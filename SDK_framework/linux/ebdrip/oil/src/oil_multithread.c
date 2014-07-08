/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_multithread.c(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup OIL
 *  \brief The main multi-threaded entry point to the OIL.
 *
 * This file contains the function used to start the RIP in a separate thread.
 *
 */
#include "pms_export.h"
#include "oil.h"
#include "oil_main.h"
#include "oil_multithread.h"
#include "oil_job_handler.h"
#include "oil_ebddev.h"
#include "oil_psconfig.h"
#include "oil_stream.h"
#include "oil_interface_oil2pms.h"
#include "oil_pdfspool.h"
#include "oil_entry.h"

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;



/**
 * \brief Entry point for the OIL in a multi-threaded implementation.
 *
 * This routine is the main entry to the OIL.\n
 * This function is called by OIL_Start(), and it is the role of this
 * function to start the RIP in a separate thread, read data from the
 * input stream and pass this data to the RIP.\n
 * Rasters are delivered from the RIP to the OIL via a callback specified
 * in sys_init() called OIL_MonitorCallback().
 * \param[in]    pms_ptJob  A pointer to a PMS job structure to be run.
 * \param[in]    ePDL       The PDL of the job to be run.
 * \return       Returns TRUE following successful execution, FALSE if an error occurrs.
 */
int OIL_MultiThreadedStart(PMS_TyJob *pms_ptJob, int ePDL)
{
  int fSuccess = TRUE;

  PDFSPOOL * pdfspool = NULL;

  /* Start the RIP. This function won't do a lot if the
     RIP has already been started (from an earlier call it, or
     if using partial shutdown mode).
     */
  if(!OIL_StartRIP()) {
    oil_printf("OIL_StartRIP failed.\n");
    return FALSE;
  }

  /* create (and initialize) the OIL job structure */
  g_pstCurrentJob = CreateOILJob(pms_ptJob, ePDL);

  ebddev_InitDevParams();
  {
    /* tell RIP we are starting a job */
    if(!JobInit(OIL_Sys_JobActive))
    {
      /* cleanup will be done below in JobExit() */
      fSuccess = FALSE;
    }

    /* get job data from PMS and pass it on to RIP */
    if( fSuccess )
    {
      if( ePDL == OIL_PDL_PDF )
      {
        if(!g_pstCurrentJob->bFileInput)
        {
          pdfspool = OIL_PdfSpool_Create();
          if( pdfspool == NULL || ! OIL_PdfSpool_StoreData(pdfspool) )
          {
            /* Failed to spool PDF */
            fSuccess = FALSE;
          }
        }
      }

      /* Configure RIP for PDL */
      if ( fSuccess && ePDL != OIL_PDL_PS && !SetupPDLType( TRUE, NULL, 0 ) )
      {
        GG_SHOW(GG_SHOW_OIL, "OIL_MultiThreadedStart: failed to initialise PDL\n");
        fSuccess = FALSE;
      }

      g_pstCurrentJob->eJobStatus = OIL_Job_StreamDone;
    }
  }

  /* End of job */
  if(!JobExit(OIL_Sys_Active))
  {
    fSuccess = FALSE;
  }

  if( pdfspool != NULL )
  {
    OIL_PdfSpool_Free(pdfspool);
  }

  /* Shutdown the rip (if shutdown mode allows it). */
  OIL_StopRIP(FALSE);

  return fSuccess;
}


