/* Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_main.c(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup OIL
 *  \brief Implementation of the OIL support functions.
 *
 * This file contains the definitions of the functions which initialize and inactivate the OIL
 * and/or individual jobs, as well as various other support functions, including functions
 * for registering resources and RIP modules with the RIP.
 *
 */
#include "oil_interface_oil2pms.h"
#include <string.h>
#include "skinkit.h"
#include "pgbdev.h"
#include "mem.h"
#include "pms_export.h"
#include "oil_platform.h"
#include "oil.h"
#include "oil_interface_skin2oil.h"
#include "oil_job_handler.h"
#if defined(SDK_SUPPORT_2BPP_EXT_EG) || defined(SDK_SUPPORT_4BPP_EXT_EG)
#include "oil_htm.h"
#endif
#include "oil_cmm.h"
#include "oil_ebddev.h"
#include "oil_page_handler.h"
#include "oil_probelog.h"
#include "oil_progdev.h"
#include "oil_psconfig.h"
#include "oil_main.h"
#include "oil_malloc.h"
#include "oil_media.h"
#include "oil_timing.h"
#include "oil_stream.h"
#include "oil_virtualfile.h"
#include "pdfspool.h"
#include "swpfinpcl.h"
#include "rdrapi.h"
#include "timelineapi.h"
#include "timer.h"
#include "progevts.h"
#include "swevents.h"
#ifdef USE_UFST5
#include "pfinufst5.h"
#endif
#ifdef USE_UFST7
#include "pfinufst7.h"
#endif
#ifdef USE_FF
#include "pfinff.h"
#endif
#include "libjpeg.h"
#include "libjpegturbo.h"

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;
extern OIL_TyPage *g_pstCurrentPage;
extern OIL_TyConfigurableFeatures g_NextJobConfiguration;
extern int g_bLogTiming;
extern OIL_TyError g_JobErrorData;
extern const char *g_mps_log ;
extern unsigned long g_mps_telemetry ;
extern const char *g_profile_scope ;

/* Addition DEVICETYPEs for embedded skin */
DEVICETYPE * ppEmbeddedDevices[] =
{
  &EBD_Progress_Device_Type,
  &Embedded_Device_Type,
  &PDF_Spooler_Device_Type
};

/* Memory callback functions, these are used by the skin to provide memory allocation */
static void *RIPCALL SysAllocFnCallback(size_t cbSize)
{
  return (OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, cbSize));
}

static void RIPCALL SysFreeFnCallback(void * ptr)
{
  OIL_free(OILMemoryPoolJob, ptr);
}

static void flushInput(void)
{
  int           nbytes_read;
  unsigned char buffer[ OIL_READBUFFER_LEN ];

  /* Simplistic flushing of input.
   * Maybe should look for indicators of start and end of job?
   */
  do
  {
    nbytes_read = PMS_PeekDataStream( buffer, sizeof(buffer) );

    if( nbytes_read > 0 )
    {
      PMS_ConsumeDataStream( nbytes_read );
    }
  } while( nbytes_read > 0 );
}

/**
 * \brief Callback function invoked when the RIP exits.
 *
 * This function is registered with the RIP via SwLeSetRipExitFunction
 * and is intended to perform tidy up operations when the RIP exits.
 * The remainder of any active job is discarded by flushing the input.
 *
 * Note that this function is called whenever the RIP exits, so will also
 * be called as a result of calling \c SwLeStop().
 * \param[in]   errorCode   Error code passed from the RIP to this function; used to create an exit message.
 * \param[in]   pszText     Exit message passed from the RIP to this function; used to create an exit message.
 */
static void OIL_RipExitCallback(int32 errorCode, uint8 * pszText)
{
  GG_SHOW(GG_SHOW_OIL, "OIL_RipExitCallback: RIP exited (%d)\n", errorCode);
  if( pszText != NULL )
  {
    /* Force output of pszText message */
    unsigned int showflags = g_ConfigurableFeatures.g_uGGShow;

    g_ConfigurableFeatures.g_uGGShow |= GG_SHOW_OIL;
    GG_SHOW(GG_SHOW_OIL, "    %s\n", pszText);
    g_ConfigurableFeatures.g_uGGShow = showflags;
  }

  if( g_SystemState.eCurrentState == OIL_Sys_JobActive )
  {
    /* RIP exited in the middle of a job (so not as a result of calling SwLeStop() */
    g_SystemState.eCurrentState = OIL_Sys_Inactive;

    /* Read and discard any remaining input */
    GG_SHOW(GG_SHOW_OIL, "OIL_RipExitCallback: Flushing input\n");
    flushInput();
  }
}

/**
 * \brief Callback function invoked when the RIP reboots
 *
 * This function is registered with the RIP via SwLeSetRipRebootFunction
 * and is intended to perform tidy up operations when the RIP reboots during.
 * a job. The remainder of the job is discarded by flushing the input.
 */
static void OIL_RipRebootCallback(void)
{
  GG_SHOW(GG_SHOW_OIL, "OIL_RipRebootCallback: RIP rebooted\n");

  /* Read and discard any remaining input */
  GG_SHOW(GG_SHOW_OIL, "OIL_RipRebootCallback: Flushing input\n");
  flushInput();
}

/**
 * \brief  this function is a comon Event handler for all
 *
 * Simple block of halftone events
 */
static sw_event_result HQNCALL mon_info_func(void * context, sw_event * ev)
{
  UNUSED_PARAM(void *, context) ;
  UNUSED_PARAM(sw_event *, ev) ;

  return SW_EVENT_HANDLED;
}
/**
 * \brief  this stucture defines the event handling for oil
 *
 * Currently only halftone events are overridden to remove all the information messages
 * that are set up as default
 */
static sw_event_handlers handlers[] = {
  { mon_info_func, NULL, 0, SWEVT_HT_GENERATION_SEARCH, SW_EVENT_OVERRIDE },
  { mon_info_func,  NULL, 0, SWEVT_HT_GENERATION_START,  SW_EVENT_OVERRIDE },
  { mon_info_func,    NULL, 0, SWEVT_HT_GENERATION_END,    SW_EVENT_OVERRIDE },
  { mon_info_func,   NULL, 0, SWEVT_HT_USAGE_THRESHOLD,   SW_EVENT_OVERRIDE },
  { mon_info_func,        NULL, 0, SWEVT_HT_USAGE_SPOT,        SW_EVENT_OVERRIDE },
  { mon_info_func,     NULL, 0, SWEVT_HT_USAGE_MODULAR,     SW_EVENT_OVERRIDE },
  { mon_info_func,    NULL, 0, SWEVT_HT_USAGE_COLORANT,    SW_EVENT_OVERRIDE },
};

/**
 * \brief  this function registers the event handling for oil as defined in the structure above
 *
 */
static void oil_events_initialise(void)
{
  /* override the halftone monitor output to remove unwanted messages */
  SwRegisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers));
}
/**
 * \brief  this function de-registers the event handling for oil
 *
 */
static void oil_events_finish(void)
{
  (void)SwDeregisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) ;
}

/**
 * \brief OIL system initialization function.
 *
 * Calling this function initializes the OIL and starts the RIP.  The function
 * first checks to ensure that the OIL is not in the \c OIL_Sys_JobActive or
 * \c OIL_Sys_Uninitialised states.  This function:
 * \arg sets up the callback functions used by the Skin to allocate and free memory.
 * \n In addition, if the system is currently in the \c OIL_Sys_Inactive state and the next state
 * requested is the \c OIL_Sys_Active state, this function:
 * \arg allocates and initializes RIP memory;
 * \arg provides a list of embedded devices to the RIP;
 * \arg provides exit and reboot callback functions to the RIP;
 * \arg starts the RIP;
 * \arg registers resources, RIP modules and streams as required, and
 * \arg places the system into the \c OIL_Sys_Active state.
 * \n If the system is currently in the \c OIL_Sys_Suspended state, and the next state
 * requested is the \c OIL_Sys_Active state, this function:
 * \arg places the system into the \c OIL_Sys_Active state.
 *
 * During this call the RIP is started and claims
 * \c g_SystemState.cbRIPMemory bytes of memory.
 *
 * \param[in]   eNextState  The system state that is required by the end of the call;
 *              expected to be \c OIL_Sys_Active.
 * \return      Returns TRUE if the system is successfully initialized (as necessary)
 *              and placed in the requested state, FALSE otherwise.
 */
int SysInit(OIL_eTySystemState eNextState)
{
  struct SysMemFns tSkinMemoryFns;

  HQASSERT((OIL_Sys_JobActive != g_SystemState.eCurrentState),
           ("sys_init entered in state: OIL_Sys_JobActive"));
  HQASSERT((OIL_Sys_Uninitialised != g_SystemState.eCurrentState),
           ("sys_init entered in state: OIL_Sys_Uninitialised"));

  /* Initialise the memory helper functions */
  /* - these are the memory allocation functions to be used by skin */
  tSkinMemoryFns.pAllocFn = SysAllocFnCallback;
  tSkinMemoryFns.pFreeFn = SysFreeFnCallback;

  GG_SHOW(GG_SHOW_OIL, "SysInit:\n");

  if ( g_profile_scope != NULL &&
       !SwLeProfileOption(NULL, g_profile_scope) ) {
    oil_printf("sys_init: Invalid profile scope %s\n", g_profile_scope);
    return 0;
  }

  /* log the job start time */
  GGglobal_timing(SW_TRACE_OIL_SYSSTART, 0);

  if ((g_SystemState.eCurrentState == OIL_Sys_Inactive) && (eNextState == OIL_Sys_Active))
  {
    uint8 *reasonText ;

    /* allocate memory for the RIP, all systems ready for a job */
    g_SystemState.pRIPMemory = NULL;
    g_SystemState.pRIPMemory = OIL_malloc(OILMemoryPoolSys, OIL_MemBlock, g_SystemState.cbRIPMemory);
    if(g_SystemState.pRIPMemory == NULL) /*Bad Pointer*/
    {
      oil_printf("sys_init: Failed to allocate RIP Memory\n");
      return 0;
    }

    (void)SwLeInitRuntime(NULL) ;

    MemLogInit(g_mps_log, g_mps_telemetry) ;

    /* It is assumed that pthreads has been initialised. This is done
       earlier in the OIL boot process. */

    /* initialize the SDK support libraries */
    g_SystemState.cbmaxAddressSpace = g_SystemState.cbRIPMemory;
    if ( !SwLeSDKStart(&g_SystemState.cbmaxAddressSpace,
                       &g_SystemState.cbRIPMemory,
                       g_SystemState.pRIPMemory,
                       &tSkinMemoryFns,
                       &reasonText) ) {
      GG_SHOW(GG_SHOW_OIL, "sys_init: %s.\n", (char *)reasonText) ;
      return FALSE ;
    }

    /* Set number of RIP renderer threads */
    SwLeSetRipRendererThreads( g_ConfigurableFeatures.nRendererThreads );

    SwLeMemInit(g_SystemState.cbmaxAddressSpace, g_SystemState.cbRIPMemory, 0, g_SystemState.pRIPMemory);
    /* set MultipleCopies pgbdev param to true so that we get just 1 copy in rastercallback */
    SwLePgbSetMultipleCopies(TRUE);


    SwLeSetRasterCallbacks(OIL_RasterStride,
                           OIL_RasterRequirements,
                           OIL_RasterDestination,
                           OIL_RasterCallback);
    /* Add embedded devices to list passed to RIP */
    SwLeAddCustomDevices( sizeof(ppEmbeddedDevices) / sizeof(ppEmbeddedDevices[0]), ppEmbeddedDevices );

    /* set RIP exit callback */
    SwLeSetRipExitFunction( OIL_RipExitCallback );

    /* set RIP reboot callback */
    SwLeSetRipRebootFunction( OIL_RipRebootCallback );

    if (!oil_progress_init()) {
            GG_SHOW(GG_SHOW_OIL, "sys_init: Failed to start progress timeline \n");
      return FALSE;
    }

    /* Start RIP */
    if ( SwLeStart( g_SystemState.cbmaxAddressSpace, g_SystemState.cbRIPMemory, 0, g_SystemState.pRIPMemory, (SwLeMONITORCALLBACK *)OIL_MonitorCallback ) )
   {
      g_SystemState.eCurrentState = eNextState;
    }
    else
    {
      GG_SHOW(GG_SHOW_OIL, "sys_init: Failed to start RIP \n");
      return FALSE;
    }
    /* Report job processing times */
    progevts_enable_times();
    oil_events_initialise();
    if(!RegisterResources())
    {
      GG_SHOW(GG_SHOW_OIL, "sys_init: Failed to register resources\n");
      return FALSE;
    }
    if(!RegisterRIPModules())
    {
      GG_SHOW(GG_SHOW_OIL, "sys_init: Failed to register RIP modules\n");
      return FALSE;
    }
    if(!Stream_Register())
    {
      GG_SHOW(GG_SHOW_OIL, "sys_init: Failed to register Stream\n");
      return FALSE;
    }
  }

  if ( !libjpeg_register() || !libjpegturbo_register() ) {
    GG_SHOW(GG_SHOW_OIL, "sys_init: Failed to register libjpeg\n");
    return FALSE;
  }

  if ((g_SystemState.eCurrentState == OIL_Sys_Suspended) && (eNextState == OIL_Sys_Active))
  {
    GG_SHOW(GG_SHOW_OIL, "**RIP awakes\n");
    g_SystemState.eCurrentState = eNextState;
    return TRUE;
  }
 /* Reset the job error status */
  g_JobErrorData.Code = 0;
  g_JobErrorData.bErrorPageComplete = FALSE;
  /* return TRUE if now in requested state */
  return (g_SystemState.eCurrentState == eNextState);
}

/**
 * \brief OIL system exit function.
 *
 * This function is called to exit the OIL and shut down the RIP, freeing
 * all resources claimed by the RIP.   The function first checks to ensure
 * that the OIL is not in the \c OIL_Sys_JobActive or \c OIL_Sys_Uninitialised
 * If the current system state is \c OIL_Sys_Active and the requested state is
 * \c OIL_Sys_Inactive, this function:
 * \arg unregisters streams;
 * \arg attempts to stop the RIP, and reports an error message if it is unsuccessful, and
 * \arg sets places the system into the \c OIL_Sys_Inactive state.
 * \param[in]   eNextState  The system state that is required by the end of the call; expected
 *              to be \c OIL_Sys_Inactive.
 * \return      Returns TRUE if the OIL is successfully exited and placed in the
 *              requested state, FALSE otherwise.
 */
int SysExit(OIL_eTySystemState eNextState)
{
  HQASSERT(OIL_Sys_JobActive != g_SystemState.eCurrentState,
           "sys_exit entered in state: OIL_Sys_JobActive");
  HQASSERT(OIL_Sys_Uninitialised != g_SystemState.eCurrentState,
           "sys_exit entered in state: OIL_Sys_Uninitialised");

  GG_SHOW(GG_SHOW_OIL, "SysExit:\n");
  if ((g_SystemState.eCurrentState == OIL_Sys_Active) && (eNextState == OIL_Sys_Inactive))
  {
    int val;

    libjpegturbo_deregister();
    libjpeg_deregister();
    Stream_Unregister();
    val=SwLeStop();
    HQASSERT(val==0, "sys_exit : RIP is not shutting down successfully");
    oil_progress_finish();
    oil_events_finish();

    SwLeSDKEnd(val);

    /* shutdown the RIP, free systems resources */
    SwLeShutdown(); /* does nothing in ebd, but called for consistency with start up*/
    OIL_free(OILMemoryPoolSys, g_SystemState.pRIPMemory);

    g_SystemState.eCurrentState = eNextState;
  }

  /* return TRUE if now in requested state */
  return (g_SystemState.eCurrentState == eNextState);
}

/**
 * \brief OIL job initialisation function.
 *
 * This function is called to prepare the RIP for an incoming job. It requires that the
 * OIL be in the  \c OIL_Sys_Active state when the function is entered.
 * \param[in]   eNextState  The system state that is required by the end of the call;
 *              expected to be \c OIL_Sys_JobActive.
 * \return      Returns TRUE if the job initialization is successfully performed and
 *              the system is placed in the requested state, FALSE otherwise.
 */
int JobInit(OIL_eTySystemState eNextState)
{
  uint8 * pConfigPS;
  HQASSERT(OIL_Sys_Active == g_SystemState.eCurrentState,
           "job_init entered in invalid state");

  GG_SHOW(GG_SHOW_OIL, "JobInit:\n");

  /* log the job start time */
  GGglobal_timing(SW_TRACE_OIL_JOBSTART, 0);

  /* Tell RIP we are starting a job */
  pConfigPS = GetConfigPS( g_pstCurrentJob->ePDLType );

  /* ready to start the page */
  GGglobal_timing(SW_TRACE_OIL_PAGESTART, 0);

  /* Set state now as a PS job completes during execution of SwLeJobStart */
  g_SystemState.eCurrentState = eNextState;

  if ( ! SwLeJobStart ( (uint32)strlen((char*)pConfigPS),
                      pConfigPS,
                      NULL) )
  {
    /* SwLeJobStart can return false in case of bad input, but nevertheless job is now active */
    GG_SHOW(GG_SHOW_OIL, "job_init: Failed to start job \n");
  }

  /* return TRUE if now in requested state */
  return (g_SystemState.eCurrentState == eNextState);
}

/**
 * \brief OIL job exit function.
 *
 * This function is called to notify the RIP that a job has completed and
 * that the resources claimed by the job can now be freed.  It requires that
 * the system be in one of \c OIL_Sys_JobCancel, \c OIL_Sys_JobActive or
 * \c OIL_Sys_Inactive states when the function is entered.\n
 * This function:
 * \arg requests that the RIP end the current job;
 * \arg discards remaining pages in the job;
 * \arg cleans up any partial pages;
 * \arg deletes the job;
 * \arg configures the RIP according to the settings in the next job, if any, and
 * \arg places the system into the next requested state, if it is not already inactive.
 *
 * This function does not cause the RIP to exit.
 *
 * \param[in]   eNextState  The system state that is required by the end of the call.
 * \return      Returns TRUE if the post-job cleanup is successful and the OIL is
 *              placed in the requested state, FALSE otherwise.
 */
int JobExit(OIL_eTySystemState eNextState)
{
  char szJobName[OIL_MAX_JOBNAME_LENGTH];

  HQASSERT(OIL_Sys_JobCancel == g_SystemState.eCurrentState ||
           OIL_Sys_JobActive == g_SystemState.eCurrentState ||
           OIL_Sys_Inactive == g_SystemState.eCurrentState,
           "job_exit entered in invalid state");

  GG_SHOW(GG_SHOW_OIL, "JobExit:\n");

  /* release resources */
  while (g_ulGGtiming_pagecount != 0)
  {
    /* wait - until all the pages have been output */
    OIL_RelinquishTimeSlice();
  }
  /*  free(g_pstCurrentJob);  */

  {
    if (!SwLeJobEnd())
    {
      /* Report this error */
      GG_SHOW(GG_SHOW_OIL, "job_exit: Failed to finish job \n");
    }
  }

  if(OIL_Sys_Inactive != g_SystemState.eCurrentState)
  {
    g_SystemState.eCurrentState = eNextState;
  }

  /* At this point RIPping must have completed and oil should have received data for all pages from the RIP*/
  g_pstCurrentJob->eJobStatus = OIL_Job_RipDone;

  /* keep the jobname safe for use in timing log before deleting job */
  strcpy(szJobName, g_pstCurrentJob->szJobName);

  /* Wait for all pages to be removed */
  while(g_pstCurrentJob->pPage!=NULL)
  {
    OIL_RelinquishTimeSlice();
  }

  /* check for incomplete pages which can be a result of job cancel */
  if (g_pstCurrentPage)
  {
    DeleteOILPage(g_pstCurrentPage);
    g_pstCurrentPage = 0;
  }

  /* Delete the job */
  DeleteOILJob(g_pstCurrentJob->uJobId);
  g_pstCurrentJob = NULL;

  /* Reconfigure the features */
  g_ConfigurableFeatures.g_ePaperSelectMode = g_NextJobConfiguration.g_ePaperSelectMode;
  g_ConfigurableFeatures.g_uGGShow = g_NextJobConfiguration.g_uGGShow;


  /* print out the timing data */
  if(g_bLogTiming)
  {
    GGglobal_timing_dumplog((unsigned char*)szJobName);
  }

  /* reset data for next job */
  GGglobal_timing(SW_TRACE_OIL_RESET, 0);


  /* return TRUE if now in requested state */
  return (g_SystemState.eCurrentState == eNextState);
}

#if defined(USE_UFST5) || defined(USE_UFST7) || defined(USE_FF)
/**
 * \brief Converts PMS priority values into RDR priorities.
 *
 * This function maps the high, normal and low priority values defined in the PMS
 * to the equivalent, but numerically different, values defined in the ROM data
 * resource (RDR) API.  If an unrecognised priority value is supplied, an error message
 * is generated and the function returns \c SW_RDR_NORMAL.
 *
 * \param[in]   ePriority  The priority value as defined by the PMS.
 * \return      Returns a \c sw_rdr_priority value which is equivalent to the PMS priority supplied.
                If the supplied value is not recognised, \c SW_RDR_NORMAL is returned.
 */
static sw_rdr_priority MapPriority( int ePriority )
{
  int ripPriority;

  switch( ePriority )
  {
  case EPMS_Priority_Low:
    ripPriority = SW_RDR_DEFAULT;
    break;
  case EPMS_Priority_Normal:
    ripPriority = SW_RDR_NORMAL;
    break;
  case EPMS_Priority_High:
    ripPriority = SW_RDR_OVERRIDE;
    break;
  default:
    HQFAILV(("MapPriority: Unknown priority %d", ePriority));
    ripPriority = SW_RDR_NORMAL;
    break;
  }

  return ripPriority;
}

/**
 * \brief Register an RDR resource type with the RIP.
 *
 * This function registers resources required for UFST-5 use with the RIP.
 *
 * \return This function returns TRUE if the resource is successfully registered,
 * or FALSE otherwise.
 */
static int RegisterResourceType( int eResourceType, sw_rdr_class rdr_class, sw_rdr_type rdr_type )
{
  int            index = 0;
  PMS_TyResource resource;

  while( PMS_GetResource( eResourceType, index, &resource ) == 0 )
  {
    sw_rdr_priority priority = MapPriority( resource.ePriority );
    sw_rdr_result result = SwRegisterRDR( rdr_class, rdr_type, resource.id, resource.pData, resource.length, priority );

    if( result != SW_RDR_SUCCESS )
    {
      HQFAILV(("RegisterResourceType: SwRegisterRDR() failed: %d", result));
      return FALSE;
    }

    index++;
  }

  return TRUE;
}

#endif

int RegisterResources( void )
{
  int fSuccess = TRUE;

#if defined(USE_UFST5) || defined(USE_UFST7)
/** \fn RegisterResources
 * \brief Register RDR resource types with the RIP.
 *
 * This function registers resources required for UFST-5 use with the RIP.
 *
 * \return This function returns TRUE if all resources are successfully registered,
 * or FALSE otherwise.
 */

  fSuccess = fSuccess
    && RegisterResourceType( EPMS_FONT_PCL_BITMAP, RDR_CLASS_FONT, RDR_FONT_PCLEO )
    && RegisterResourceType( EPMS_PCL_XLMAP, RDR_CLASS_PCL, RDR_PCL_XLMAP )
    && RegisterResourceType( EPMS_PCL_SSMAP, RDR_CLASS_PCL, RDR_PCL_SSMAP )
    && RegisterResourceType( EPMS_PCL_GLYPHS, RDR_CLASS_PCL, RDR_PCL_GLYPHS )
    && RegisterResourceType( EPMS_UFST_FCO, RDR_CLASS_FONT, RDR_FONT_FCO )
    && RegisterResourceType( EPMS_UFST_SS, RDR_CLASS_PCL, RDR_PCL_SS )
    && RegisterResourceType( EPMS_UFST_MAP, RDR_CLASS_PCL, RDR_PCL_MAP )
    ;
#endif

#ifdef USE_FF
  /*
   * Font Fusion can share some of the RDR data types as used by UFST.
   * It registers them under its own RDR type names, in case the data instances
   * differ for FontFusion compared to UFST.
   * NB: currently, Font Fusion can reuse the lineprinter PCLEO, glyphname list
   * and XL font map data that UFST uses. The same data is registered in RDR
   * twice.
   */
  fSuccess = fSuccess
    && RegisterResourceType( EPMS_FONT_PCL_BITMAP,
                             RDR_CLASS_FONT, RDR_FONT_FF_PCLEO )
    && RegisterResourceType( EPMS_PCL_XLMAP, RDR_CLASS_PCL, RDR_PCL_FF_XLMAP )
    && RegisterResourceType( EPMS_PCL_GLYPHS, RDR_CLASS_PCL, RDR_PCL_FF_GLYPHS )
    && RegisterResourceType( EPMS_FF_PFR, RDR_CLASS_FONT, RDR_FONT_PFR )
    && RegisterResourceType( EPMS_FF_FIT, RDR_CLASS_PCL, RDR_PCL_FF_FIT )
    && RegisterResourceType( EPMS_FF_MAP, RDR_CLASS_PCL, RDR_PCL_FF_MAP )
    && RegisterResourceType( EPMS_FF_SYMSET, RDR_CLASS_PCL, RDR_PCL_FF_SS )
    && RegisterResourceType( EPMS_FF_SYMSET_MAPDATA,
                             RDR_CLASS_PCL, RDR_PCL_FF_SS_MAPDATA )
    ;

#endif

  return fSuccess;
}

/**
 * \brief Register RIP modules
 *
 * Register available halftone, color management and font modules.
 */
int RegisterRIPModules(void)
{
#if defined(SDK_SUPPORT_2BPP_EXT_EG)
  sw_htm_api *pHtm2bpp;
#endif
#if defined(SDK_SUPPORT_4BPP_EXT_EG)
  sw_htm_api *pHtm4bpp;
#endif
  sw_cmm_api *pCMM;
#if defined(USE_UFST5) || defined(USE_UFST7)
  int nRetVal;
  pfin_ufst5_fns ufst5FnTable = { NULL };

  ufst5FnTable.pfnCGIFfco_Access  = (CGIFfco_AccessFn *)  (*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Access];
  ufst5FnTable.pfnCGIFfco_Plugin  = (CGIFfco_PluginFn *)  (*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Plugin];
  ufst5FnTable.pfnCGIFfco_Open    = (CGIFfco_OpenFn *)    (*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Open];
  ufst5FnTable.pfnCGIFinitRomInfo = (CGIFinitRomInfoFn *) (*g_apfn_pms_calls)[EPMS_FN_CGIFinitRomInfo];
  ufst5FnTable.pfnCGIFenter       = (CGIFenterFn *)       (*g_apfn_pms_calls)[EPMS_FN_CGIFenter];
  ufst5FnTable.pfnCGIFconfig      = (CGIFconfigFn *)      (*g_apfn_pms_calls)[EPMS_FN_CGIFconfig];
  ufst5FnTable.pfnCGIFinit        = (CGIFinitFn *)        (*g_apfn_pms_calls)[EPMS_FN_CGIFinit];
  ufst5FnTable.pfnCGIFfco_Close   = (CGIFfco_CloseFn *)   (*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Close];
  ufst5FnTable.pfnCGIFmakechar    = (CGIFmakecharFn *)    (*g_apfn_pms_calls)[EPMS_FN_CGIFmakechar];
  ufst5FnTable.pfnCGIFchar_size   = (CGIFchar_sizeFn *)   (*g_apfn_pms_calls)[EPMS_FN_CGIFchar_size];
  ufst5FnTable.pfnCGIFfont        = (CGIFfontFn *)        (*g_apfn_pms_calls)[EPMS_FN_CGIFfont];
  ufst5FnTable.pfnPCLswapHdr      = (PCLswapHdrFn *)      (*g_apfn_pms_calls)[EPMS_FN_PCLswapHdr];
  ufst5FnTable.pfnPCLswapChar     = (PCLswapCharFn *)     (*g_apfn_pms_calls)[EPMS_FN_PCLswapChar];
#ifdef USEUFSTCALLBACKS
  ufst5FnTable.pfnUFSTSetCallbacks = (UFSTSetCallbacksFn *) (*g_apfn_pms_calls)[EPMS_FN_UFSTSetCallbacks];
#endif
#ifdef USEPMSDATAPTRFNS
  ufst5FnTable.pfnUFSTGetPS3FontDataPtr      = (UFSTGetPS3FontDataPtrFn *) (*g_apfn_pms_calls)[EPMS_FN_UFSTGetPS3FontDataPtr];
  ufst5FnTable.pfnUFSTGetWingdingFontDataPtr = (UFSTGetWingdingFontDataPtrFn *) (*g_apfn_pms_calls)[EPMS_FN_UFSTGetWingdingFontDataPtr];
  ufst5FnTable.pfnUFSTGetSymbolSetDataPtr    = (UFSTGetSymbolSetDataPtrFn *) (*g_apfn_pms_calls)[EPMS_FN_UFSTGetSymbolSetDataPtr];
  ufst5FnTable.pfnUFSTGetPluginDataPtr       = (UFSTGetPluginDataPtrFn *) (*g_apfn_pms_calls)[EPMS_FN_UFSTGetPluginDataPtr];
#endif

  nRetVal = pfin_ufst5_module(&ufst5FnTable);
  if(nRetVal)
  {
    HQFAILV(("RegisterRIPModules: Failed to start initialise pfin ufst5 module %d", nRetVal));
    return FALSE;
  }
#endif

#ifdef USE_FF
  {
    int nRetVal;
    pfin_ff_fns ffFnTable = {0}; /* No FontFusion functions defined yet. */

    nRetVal = pfin_ff_module(&ffFnTable);
    if (nRetVal) {
      HQFAILV(("RegisterRIPModules: Failed to start initialise pfin ff module %d\n", nRetVal));
    }
  }
#endif

#if defined(SDK_SUPPORT_4BPP_EXT_EG)
  /* Register example simple 4-bit screening  */
  pHtm4bpp = htm4bpp_getInstance();
  if (SwRegisterHTM (pHtm4bpp) != SW_API_REGISTERED)
    return FALSE;
#endif

#if defined(SDK_SUPPORT_2BPP_EXT_EG)
  /* Register example simple 2-bit screening  */
  pHtm2bpp = htm2bpp_getInstance();
  if (SwRegisterHTM (pHtm2bpp) != SW_API_REGISTERED)
    return FALSE;
#endif

  /* Register example CMM  */
  pCMM = oilccs_getInstance();
  if (SwRegisterCMM (pCMM) != SW_API_REGISTERED)
    return FALSE;

  return TRUE;
}


