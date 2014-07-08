/* Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_entry.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief OIL interface functions.
 *
 * This file contains the implementations of the oil interface functions
 * which are exposed to the PMS.
 *
 */

#include "oil_interface_oil2pms.h"
#include "std.h"
#include "rdrapi.h"
#include "swevents.h"
#include "skinkit.h"
#include "oil.h"
#include "pms_export.h"
#include "oil_multithread.h"
#include "oil_interface_skin2oil.h"
#include "oil_psconfig.h"
#include "oil_malloc.h"
#include "oil_platform.h"
#include "oil_main.h"
#include "oil_stream.h"
#include <string.h>

#ifdef USE_PJL
#include "oil_pjl.h"
#include "pjlparser.h"
#endif

#include "oil_probelog.h"
#include "oil_virtualfile.h"
#include "ripversn.h"

#ifdef GG_DEBUG
#define DEBUG_STR   "D"
#else
#define DEBUG_STR   "V"
#endif

/* Version information */
/*
 * Format = Va.bcde
 * a = 0 for pre-prelease, 1 for final candidate
 * b = 0 for alpha
 *   = 1-9 for beta
 * c = 0-9 for interval release - eg beta 3
 * d = a-z for internal engineering releases
 * e = TBD
 * eg V0.12   = external release Beta 2
 * NOTE: external releases should have no letters eg V0.11
 */

#define OIL_BUILD_INFO " "
#define MAX_SIGNATURE_BYTES 4

static char zip_header[4] = {0x50, 0x4b, 0x03, 0x04};
static char pdf_header[4] = { '%', 'P', 'D', 'F' } ;
static char ps_header[2] = { '%', '!' } ;

/*! \brief OIL version number
*/
char    *OILVersion = DEBUG_STR "4.0r0" OIL_BUILD_INFO;

/*! \brief RIP build version
*/
char    *RIPVersion = PRODUCT_VERSION_QUOTED;

OIL_tyRIPFeatures  stRIPFeatures;
static void GetRIPFeatures(OIL_tyRIPFeatures  *);
static void ShowRIPFeatures();
extern char *GG_build_variants[];      /* corerip features */

/* Globals used by various bits of OIL */

/*! \brief Always points to the current job's OIL_TyJob structure */
OIL_TyJob *g_pstCurrentJob;
/*! \brief Always points to the current page's OIL_TyPage structure */
OIL_TyPage *g_pstCurrentPage;
/*! \brief Holds the OIL's current state, memory usage information, and user data. */
OIL_TySystem g_SystemState;
/*! \brief Holds the current configuration of the OIL's configurable features */
OIL_TyConfigurableFeatures g_ConfigurableFeatures;
/*! \brief Holds next current configuration of the OIL's configurable features */
OIL_TyConfigurableFeatures g_NextJobConfiguration;
/*! \brief Holds the error data if encountered in a job. */
OIL_TyError g_JobErrorData;
/*! \brief Holds the time at which OIL timing started. */
int g_nTimeInit;
/*! \brief Toggles timing dumplog (1 = ON). */
int g_bLogTiming;


/*! \brief Enable page checksum  (1 = ON). */
int g_bPageChecksum;


/**************************/
/* oil external functions */
/**************************/


/**
 * \brief Initialization routine for the OIL.
 *
 * This routine must be called by PMS during startup to initialize
 * critical strucutres in the OIL.  It populates callback functions with
 * function pointers passed in from the PMS. and places the system into
 * the @c OIL_Sys_Uninitialised state.
 * @param[in] apfn_funcs An array of function pointers to callbacks in the PMS.
 */

int OIL_Init(void (**apfn_funcs[])())
{
  char *pSrc, *pDst;
  char szProbeOptions[MAX_PROBE_ARG_LEN + 1];

  /* hook up the HQNc-standard  assertion handlers */
  HqAssertHandlers_t assert_handlers = { HqCustomAssert, HqCustomTrace } ;

  SetHqAssertHandlers(&assert_handlers) ;
  /*Start pthreads if not part of the OS. */
  if (! OIL_Init_platform())
    return 0;

  /* Initialise bare essential structures, this memory will never be released */
  g_pstCurrentJob = NULL;
  g_SystemState.eCurrentState = OIL_Sys_Uninitialised;
  g_SystemState.pUserData = NULL;
  g_SystemState.bJobCancelReq = FALSE;
  g_ConfigurableFeatures.bGenoaCompliance = TRUE;
  g_ConfigurableFeatures.bScalableConsumption = FALSE;
  g_ConfigurableFeatures.bImageDecimation = FALSE;
  g_ConfigurableFeatures.bPixelInterleaving = FALSE;

  g_bLogTiming = FALSE;
  g_bPageChecksum = FALSE;

  /* Initialise g_nTimeInit = 0 to be used inside OIL_TimeInMilliSecs */
  g_nTimeInit = 0;
  g_nTimeInit = OIL_TimeInMilliSecs();

  /* all parameters initialized here must also be set in JobExit() */
  g_ConfigurableFeatures.g_eShutdownMode = g_NextJobConfiguration.g_eShutdownMode = OIL_RIPShutdownPartial;

  /* initialise GG_SHOW control param to output only oil version messages - for all msgs set to 0xffff */
  g_ConfigurableFeatures.g_uGGShow = GG_SHOW_OILVER;
  if((g_tSystemInfo.nOILconfig & 0x40) != 0)
    g_ConfigurableFeatures.g_uGGShow |= GG_SHOW_JOBCFG;
  g_NextJobConfiguration.g_uGGShow = g_ConfigurableFeatures.g_uGGShow;
  g_ConfigurableFeatures.fEbdTrapping = 0.0f;
  g_ConfigurableFeatures.uColorManagement = 0;    /* Color management disabled */
  g_ConfigurableFeatures.bRetainedRaster = FALSE; /* Set to TRUE to enable */
  g_ConfigurableFeatures.bRasterByteSWap = FALSE; /* Set to TRUE to enable */
#if USE_RAM_SW_FOLDER
  g_ConfigurableFeatures.bSWinRAM = 1;
#else
  g_ConfigurableFeatures.bSWinRAM = 0;
#endif

#if defined(USE_UFST5)
  g_ConfigurableFeatures.bUseUFST5 = 1;
#else
  g_ConfigurableFeatures.bUseUFST5 = 0;
#endif

#if defined(USE_UFST7)
  g_ConfigurableFeatures.bUseUFST7 = 1;
#else
  g_ConfigurableFeatures.bUseUFST7 = 0;
#endif

#ifdef USE_FF
  g_ConfigurableFeatures.bUseFF = 1;
#else
  g_ConfigurableFeatures.bUseFF = 0;
#endif

  /* initialize the PMS callback pointers */
  *g_apfn_pms_calls = (void *)apfn_funcs;
  g_SystemState.eCurrentState = OIL_Sys_Inactive;
  GGglobal_timing(SW_TRACE_OIL_RESET, 0);

   /* Configure some configurable features from PMS */
  GetPMSSystemInfo();

  GetRIPFeatures(&stRIPFeatures);

  g_NextJobConfiguration.g_ePaperSelectMode = g_ConfigurableFeatures.g_ePaperSelectMode;

  /* Enable Scalable Consumption rip configuration */
  if(g_SystemState.nOILconfig & 0x01)
  {
    g_ConfigurableFeatures.bScalableConsumption = TRUE;
  }

  /* if OIL timing enabled, set flag */
  if(g_SystemState.nOILconfig & 0x02)
  {
    g_bLogTiming = TRUE;
    g_ConfigurableFeatures.g_uGGShow = g_NextJobConfiguration.g_uGGShow |= GG_SHOW_TIMING;
  }

  /* if page checksum enable, set flag */
  if(g_SystemState.nOILconfig & 0x04)
  {
    g_bPageChecksum = TRUE;
    g_ConfigurableFeatures.g_uGGShow = g_NextJobConfiguration.g_uGGShow |= GG_SHOW_CHECKSUM;
  }

  /* if Genoa compliance bit set, disable genoa settings */
  if(g_SystemState.nOILconfig & 0x08)
  {
    g_ConfigurableFeatures.bGenoaCompliance = FALSE;
  }

  /* if Image Decimation bit set, disable Image Decimation
   * Image Decimation command line control is disabled until further notice.
  if(g_SystemState.nOILconfig & 0x10)
  {
    g_ConfigurableFeatures.bImageDecimation = FALSE;
  }
  */

  /* if retained raster bit is set, enable retained raster settings */
  if((g_SystemState.nOILconfig & 0x20)  && (stRIPFeatures.bPDF == TRUE))
  {
    g_ConfigurableFeatures.bRetainedRaster = TRUE;
  }

  /* if raster byte swap bit is set, enable raster byte swap setting */
  if(g_SystemState.nOILconfig & 0x80)
  {
    g_ConfigurableFeatures.bRasterByteSWap = TRUE;
  }

  if((g_ConfigurableFeatures.szProbeTrace[0] != '\0') || g_bLogTiming)
  {
    /* initialise probe handler function pointers */
    OIL_ProbeLogInit("-b", "ebdprobe.log") ;
  }

  if(g_ConfigurableFeatures.szProbeTrace[0] != '\0')
  {
    pSrc = &g_ConfigurableFeatures.szProbeTrace[0];
    pDst = &szProbeOptions[0];
    for(; *pSrc; pSrc++)
    {
      if(*pSrc=='\"')
        continue;
      else if(*pSrc==' ') {
        *pDst++ = '\0';
        while(*pSrc && *pSrc==' ') pSrc++;
        if(!*pSrc)
          break;
        pSrc--;
      }
      else
        *pDst++=*pSrc;
    }
    *pDst++='\0';
    *pDst='\0';

    for(pSrc = &szProbeOptions[0]; *pSrc; pSrc++)
    {
      /* Probe logging feature using default handler in skinkit */
      if ( !OIL_ProbeOption(NULL, pSrc) )
      {
        OIL_ProbeOptionUsage() ;
        OIL_ProbeLogFinish() ;
        break;
      }
      pSrc+=strlen(pSrc);
    }
  }

  GG_SHOW(GG_SHOW_OILVER, "OIL Version: %s - ", OILVersion);
  ShowRIPFeatures();
  GG_SHOW(GG_SHOW_OILVER, "\r\n");

#ifdef USE_PJL
  OIL_PjlInit();
#endif
  return 1;
}

/**
 * \brief Determine the PDL from the data stream.
 *
 * This function examines the data in the supplied job to determine the PDL.
#ifdef USE_PJL
 * First, it parses the data for PJL until PDL data is detected or data is exhausted.
#endif
 *
 * \param[in]  pms_ptJob The job whose PDL is to be detected.
 * \param[out] pPDL     Pointer to receive the PDL.  This is set to one of the OIL_eTyPDLType
 *                      values if a PDL is detected.
 *
 * \return TRUE if a PDL is detected, FALSE otherwise.
 */
static int32 OIL_DeterminePDL( PMS_TyJob *pms_ptJob, int * pPDL )
{
  int32 fGotPDL = FALSE;

  unsigned char buffer[ OIL_READBUFFER_LEN ];
  size_t cbBytesInBuffer = 0;
  int eOIL_PDL = OIL_PDL_Unknown;

#ifdef USE_PJL
  size_t cbConsumed = 0;
  int ePJL_PDL = eUnknown;

  /* Parse any PJL */
  cbBytesInBuffer = PMS_PeekDataStream( buffer, sizeof(buffer) );

  do
  {
    ePJL_PDL = OIL_PjlParseData( buffer, cbBytesInBuffer, &cbConsumed );

    if( cbConsumed > 0 )
    {
      /* Tell PMS we've consumed some data */
      PMS_ConsumeDataStream( cbConsumed );
    }

    if( ePJL_PDL != eNeedMoreData && ePJL_PDL != ePJL )
    {
      /* Found a PDL */
      break;
    }

    cbBytesInBuffer = PMS_PeekDataStream( buffer, sizeof(buffer) );

  } while( cbBytesInBuffer > 0 );

  if( ePJL_PDL != ePJL )
  {
    eOIL_PDL = OIL_PjlMapPdlValue( ePJL_PDL );

    if(eOIL_PDL == OIL_IMG)
    {
      pms_ptJob->bInputIsImage = TRUE;
      /* image jobs are handled by postscript interpreter */
      eOIL_PDL = OIL_PDL_PS;
    }
    else if( eOIL_PDL == OIL_PDL_Unknown )
    {
#endif




      cbBytesInBuffer = PMS_PeekDataStream( buffer, MAX_SIGNATURE_BYTES );

      if (cbBytesInBuffer < MAX_SIGNATURE_BYTES)
      {
        /* todo - can not determine PDL with less than 4 bytes, read more data */
        GG_SHOW(GG_SHOW_PSCONFIG, "OIL_DeterminePDL: too few characters read in first read to reliably determine PDL\n");
      }
      else if ( memcmp( buffer, zip_header, 4 ) == 0 )
      {
        eOIL_PDL = OIL_PDL_XPS;
      } 
      else if ( memcmp( buffer, pdf_header, 4 ) == 0 )
      {
        eOIL_PDL = OIL_PDL_PDF;
      }
      else if ( memcmp( buffer, ps_header, 2 ) == 0 )
      {
        eOIL_PDL = OIL_PDL_PS;
      }

      else
      {
        /* assume the job is PCL5 */
        eOIL_PDL = OIL_PDL_PCL5c;
      }

#ifdef USE_PJL
    }
  }
#endif
 
  fGotPDL = (eOIL_PDL != OIL_PDL_Unknown) ? TRUE : FALSE;
  *pPDL = eOIL_PDL;

  return fGotPDL;
}

/**
 * \brief The main entry point for the OIL.
 *
 * The PMS calls this routine to signal that there is work for the OIL.
 * This routine then reads data from the input stream and passes
 * the data to the RIP.
 *
 * In return, rasters are delivered from the RIP back to the OIL via a
 * callback, OIL_RasterCallback(), which is specified in JobInit().
 *
 * This routine returns once the RIP has processed the first
 * (and probably only) portion of PDL from the input stream.
#ifdef USE_PJL
 * This is before any trailing PJL has been parsed.
#endif
 * This routine must be called repeatedly in order to process
 * all of the input stream for a job.
 *
 * \param[in]  pms_ptJob The job as supplied to the function by the PMS.
 * \param[out] pbSubmittedJob Set to TRUE if the job is submitted to the RIP.
 *
 * \return TRUE if PDL data is found and processed.  Note that this means
 * that this function should be called again to ensure that all PDL data in
 * the job has been processed.  When there is no further PDL data found,
 * the function will return false.
 */
int OIL_Start( PMS_TyJob *pms_ptJob, int * pbSubmittedJob )
{
  int retVal = FALSE;

  int eOIL_PDL = OIL_PDL_Unknown;

  *pbSubmittedJob = FALSE;

#ifdef USE_PJL
  OIL_PjlSetEnvironment( pms_ptJob );
#endif

  if( OIL_DeterminePDL( pms_ptJob, &eOIL_PDL ) )
  {
    Stream_Reset();

    *pbSubmittedJob = TRUE;

    retVal = OIL_MultiThreadedStart( pms_ptJob, eOIL_PDL );
    /* If a job error is set then start a job to print it */
    if(g_JobErrorData.Code == 1)
    {
      /* modify the error code so the oil_streamread will read the error data and create a page */
      g_JobErrorData.Code = 2;
      retVal = OIL_MultiThreadedStart( pms_ptJob, OIL_PDL_PS );
    }

    /* If PDF or XPS was submitted as a file, signal that we have finished.
     * File inputs from other PDLs are streamed and close at the stream's end. */
    if( pms_ptJob->bFileInput &&
        ( eOIL_PDL == OIL_PDL_PDF || eOIL_PDL == OIL_PDL_XPS ) )
      retVal = FALSE;
  }
  else
  {
    /* No PDL data */
    retVal = FALSE;
  }

#ifdef USE_PJL
  OIL_PjlClearEnvironment();

  if( !retVal )
  {
    OIL_PjlJobFailed( pms_ptJob->uJobId );
  }
#endif

  return retVal;
}

/**
 * \brief Start the RIP.
 *
 * \return @c TRUE if RIP started, @c FALSE otherwise.
 */
int OIL_StartRIP()
{
  /* check if system already started, start up as required */
  if(!SysInit(OIL_Sys_Active))
  {
    if(g_SystemState.eCurrentState == OIL_Sys_Active)
    {
      SysExit(OIL_Sys_Inactive);
    }
    return FALSE;
  }

  return TRUE;
}

/**
 * \brief Stop the RIP.
 *
 * \param[in] bForcedShutdown If TRUE, stop RIP regardless of the shutdown mode.
 *                            Otherwise, shutdown the RIP if shutdown mode is
 *                            total shutdown.
 */
void OIL_StopRIP(int bForcedShutdown)
{
  /* check if system needs shutting down */
  if(g_SystemState.eCurrentState != OIL_Sys_Inactive)
  {
    if(bForcedShutdown || (g_NextJobConfiguration.g_eShutdownMode == OIL_RIPShutdownTotal))
    {
      g_SystemState.eCurrentState = OIL_Sys_Active;
    }
    else
    {
      GG_SHOW(GG_SHOW_OIL, "**RIP suspended\n");
      g_SystemState.eCurrentState = OIL_Sys_Suspended;
    }
  }

  /* This call doesn't do much if in partial shutdown mode */
  SysExit(OIL_Sys_Inactive);
}

/**
 * \brief Exit routine for OIL.
 *
 * This is called to shut down the OIL and free all resources.
 * If the RIP is still active, it is shut down fairly cleanly.
 */
void OIL_Exit(void)
{
#ifdef USE_PJL
  OIL_PjlExit();
#endif

  /* Quit the RIP fairly cleanly, so that SwExit is called and the
     multi-process semaphores are cleaned up properly.
     At this point, the rip may fail to quit cleanly depending upon
     the reason we're quiting therfore we ignore the return value. */

  /* if for whatever reason the RIP is not shutdown e.g offending command,
  or RIP is suspended, force to shutdown the RIP */
  if(g_SystemState.eCurrentState != OIL_Sys_Inactive)
  {
    if(g_NextJobConfiguration.g_eShutdownMode == OIL_RIPShutdownPartial)
    {
      GG_SHOW(GG_SHOW_OIL, "OIL_Exit: Performing deferred RIP shutdown for partial shutdown mode\n");
    }
    else
    {
      GG_SHOW(GG_SHOW_OIL, "OIL_Exit: Shutting down RIP, but expect SysExit to have done this by now\n");
    }
    /* Force shutdown of rip (ignore shutdown mode). */
    OIL_StopRIP(TRUE);
  }

  /* inform PMS that ripping is complete */
  if(g_bLogTiming)
  {
    GGglobal_timing_PPMlog();
  }

  PMS_RippingComplete();

  OIL_ProbeLogFinish() ;

  OIL_VirtFileCleanup();
}

/**
 * \brief Callback to indicate job complete.
 *
 * This function is called by the PMS to notify the OIL that the job has
 * been completely output and all resources used by this job can
 * be freed. This function is not implemented in this sample code.
 * \return This stub implementation always returns TRUE.
 * \todo - not implemented yet
 */
int OIL_JobDone(void)
{
  return TRUE;
}

/**
 * \brief Request cancellation of the current job.
 *
 * This function requests cancellation of the current job in the OIL.
 * Along with OIL_AbortCallback(), it forms a decoupled mechanism for
 * cancelling the current job.  Calling this function set the status
 * of the current job (if there is one) to @c OIL_Job_Abort.  By calling
 * OIL_AbortCallback() periodically, the Skin can check on the status of the
 * current job, and, if cancellation has been requested, take appropriate
 * action.
 */
void OIL_JobCancel(void)
{
  SWMSG_INTERRUPT irq = { 0 };

  (void)SwEvent(SWEVT_INTERRUPT_USER, &irq, sizeof(irq));

  /* g_SystemState.bJobCancelReq = TRUE; not required with events */
  g_SystemState.eCurrentState = OIL_Sys_JobCancel;

  if(g_pstCurrentJob)
  {
  /* g_pstCurrentJob->eJobStatus = OIL_Job_Abort; not required with events */
    g_pstCurrentJob->eJobStatus = OIL_Job_RipDone ;
  }
}
/**
 * \brief Parse the RIP configuration
 *
 * This function parses the list of configuration options used to
 * build the RIP and populates the RIP features structure accordingly.
 * \param[out] pstRIPFeatures  The RIP features structure to be populated.
 */
static void GetRIPFeatures(OIL_tyRIPFeatures *pstRIPFeatures)
{
  int i=0;

  pstRIPFeatures->bTrapping = FALSE;
  pstRIPFeatures->bCoreTrace = FALSE;
  pstRIPFeatures->bFontEmul = FALSE;
  pstRIPFeatures->bPS = FALSE;
  pstRIPFeatures->bPDF = FALSE;
  pstRIPFeatures->bPCL = FALSE;
  pstRIPFeatures->bXPS = FALSE;
  pstRIPFeatures->bMultiThreadedCore = FALSE;
  pstRIPFeatures->bUFST5 = FALSE;
  pstRIPFeatures->bUFST7 = FALSE;

  while (GG_build_variants[i]) {
    if (strcmp("corefeatures=trapping", GG_build_variants[i])==0) {
      pstRIPFeatures->bTrapping = TRUE;
    }
    else if (strcmp("coretrace=all", GG_build_variants[i])==0) {
      pstRIPFeatures->bCoreTrace = TRUE;
    }
    else if (strcmp("fontemul=yes", GG_build_variants[i])==0) {
      pstRIPFeatures->bFontEmul = TRUE;
    }
    else if (strcmp("ps=yes", GG_build_variants[i])==0) {
      pstRIPFeatures->bPS = TRUE;
    }
    else if (strcmp("pdfin=yes", GG_build_variants[i])==0) {
      pstRIPFeatures->bPDF = TRUE;
    }
    else if (strcmp("xps=yes", GG_build_variants[i])==0) {
      pstRIPFeatures->bXPS = TRUE;
    }
    else if (strcmp("pcl=pclall", GG_build_variants[i])==0) {
      pstRIPFeatures->bPCL = TRUE;
      pstRIPFeatures->bPCL5 = TRUE;
    }
    else if (strcmp("pcl=pcl5", GG_build_variants[i])==0) {
      pstRIPFeatures->bPCL5 = TRUE;
    }
    else if (strcmp("ebd_ff=effy", GG_build_variants[i])==0) {
      pstRIPFeatures->bFF = TRUE;
    }
    else if (strcmp("ebd_ufst=eufst5y", GG_build_variants[i])==0) {
      pstRIPFeatures->bUFST5 = TRUE;
    }
    else if (strcmp("ebd_ufst=eufst7y", GG_build_variants[i])==0) {
      pstRIPFeatures->bUFST7 = TRUE;
    }
    i++;
  }
}

/**
 * \brief Display the RIP configuration
 *
 * This function displays the active features, according
 * to the RIP features structure, as a string.
 */
static void ShowRIPFeatures()
{

  GG_SHOW(GG_SHOW_OILVER, "(");
  if (stRIPFeatures.bPS)
    GG_SHOW(GG_SHOW_OILVER, " PS");
  if (stRIPFeatures.bXPS)
    GG_SHOW(GG_SHOW_OILVER, " XPS");
  if (stRIPFeatures.bPDF)
    GG_SHOW(GG_SHOW_OILVER, " PDF");
  if (stRIPFeatures.bPCL5)
    GG_SHOW(GG_SHOW_OILVER, stRIPFeatures.bPCL ? " PCL" : " PCL5");
  if (stRIPFeatures.bTrapping)
    GG_SHOW(GG_SHOW_OILVER, " Trapping");
  if (stRIPFeatures.bCoreTrace)
    GG_SHOW(GG_SHOW_OILVER, " CoreTrace");
  if (stRIPFeatures.bFontEmul)
    GG_SHOW(GG_SHOW_OILVER, " FontEmul");
  if (stRIPFeatures.bMultiThreadedCore)
    GG_SHOW(GG_SHOW_OILVER, " MT");
  if(stRIPFeatures.bUFST5)
    GG_SHOW(GG_SHOW_OILVER," UFST5 ");
  if(stRIPFeatures.bUFST7)
    GG_SHOW(GG_SHOW_OILVER," UFST7 ");
  if(stRIPFeatures.bFF)
    GG_SHOW(GG_SHOW_OILVER," FF ");
  GG_SHOW(GG_SHOW_OILVER, " )\r\n");
}


