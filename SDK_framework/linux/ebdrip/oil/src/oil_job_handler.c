/* Copyright (C) 2005-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_job_handler.c(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief This file contains the implementation of the OIL's job handling functionality.
 *
 *  Job handling in the OIL is implemented via a simple linked list of OIL_TyJob structures. 
 *  Functions are provided for creating a job, deleting a job and retrieving a job from the list.
 *  Creating or deleting a job automatically adds it to or removes it from the job list.
 *
 *  Each job is identified by a unique job ID.  This must be supplied in order to delete a job
 *  or retrieve a pointer to a job.
 */

#include "oil.h"
#include "oil_malloc.h"
#include "pms_export.h"
#include <string.h>
#include "oil_job_handler.h"
#include "oil_ebddev.h"
#include "oil_interface_oil2pms.h"
#include "oil_psconfig.h"

#ifdef USE_PJL
#include "oil_pjl.h"
#include "pjlparser.h"
#endif

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyCommentParser g_CommentParser;
extern OIL_TyError g_JobErrorData;
extern unsigned int g_SocketInPort;

/*! \brief Pointer to list of active Jobs. */
static OIL_TyJobList *l_pstOilJobList;

/* internal functions' prototypes */
static void CreateOILJobNode(OIL_TyJob *pstJob);
static void DeleteOILJobNode(unsigned int JobID);

static unsigned int MapPaperSize(int ePDL, unsigned int ePMS_PaperSize);
static void dumpJOBconfig(OIL_TyJob *pstJob);

/**
 * \brief Create a new job and add it to the job list.
 *
 * This function accepts a pointer to a PMS job structure and uses it to construct an OIL 
 * job structure, which is then added to the list of OIL jobs.
 * \param[in]   pms_ptJob  A pointer to a PMS job, used to construct the OIL job.
 * \param[in]   ePDL       The PDL for the job.  Should be one of the values defined in OIL_eTyPDLType.    
 * \return      A pointer to the OIL job.
 */
OIL_TyJob * CreateOILJob(PMS_TyJob *pms_ptJob, int ePDL)
{
  OIL_TyJob *pstJob;
  unsigned int i;

  pstJob = (OIL_TyJob *) OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, sizeof(OIL_TyJob));
  HQASSERTV(pstJob!=NULL,
            ("CreateOILJob: Failed to allocate %lu bytes", (unsigned long) sizeof(OIL_TyJob)));

  pstJob->pJobPMSData = (void *)pms_ptJob;
  pstJob->uJobId = pms_ptJob->uJobId;  /* assuming that a unique job id is being passed to us by PMS */
  pstJob->eJobStatus = OIL_Job_StreamStart;          /* set job status to signify start of data stream */

  strcpy(pstJob->szHostName, pms_ptJob->szHostName);   /* set the hostname */
  strcpy(pstJob->szUserName, pms_ptJob->szUserName);   /* set the username */
  memset(g_CommentParser.szJobTitle,0,OIL_MAX_COMMENTPARSER_LENGTH);

  if( strlen((char*)pms_ptJob->szJobName) < OIL_MAX_JOBNAME_LENGTH )
  {
    strcpy(pstJob->szJobName,pms_ptJob->szJobName);
  }
  else
  {
    strncpy(pstJob->szJobName,pms_ptJob->szJobName, OIL_MAX_JOBNAME_LENGTH);
    pstJob->szJobName[OIL_MAX_JOBNAME_LENGTH - 1] = '\0';
    GG_SHOW(GG_SHOW_OIL, "CreateOILJob: Jobname truncated to %d characters \n", OIL_MAX_JOBNAME_LENGTH);
  }

  /* change backslash to forward slash */
  for(i=0;i<(strlen(pstJob->szJobName));i++)
  {
    if(pstJob->szJobName[i] == 92) /* backslash */
      pstJob->szJobName[i] = 47; /* forwardslash */
  }

  pstJob->ePDLType = ePDL;

  pstJob->bPDLInitialised = FALSE;

  pstJob->uCopyCount = pms_ptJob->uCopyCount;
  pstJob->uPagesInOIL = 0;
  pstJob->uPagesParsed = 0;
  pstJob->uPagesToPrint = 0;
  pstJob->uPagesPrinted = 0;

  pstJob->uXResolution = pms_ptJob->uXResolution;      /* set the horizontal resolution */
  pstJob->uYResolution = pms_ptJob->uYResolution;      /* set the vertical resolution */
  pstJob->uOrientation = pms_ptJob->uOrientation;

  /* initialize OIL's current media attributes by default attributes that we got from PMS */
  pstJob->tCurrentJobMedia.uInputTray = pms_ptJob->tDefaultJobMedia.uInputTray;
  pstJob->tCurrentJobMedia.uOutputTray = pms_ptJob->tDefaultJobMedia.eOutputTray;
  strcpy((char*)pstJob->tCurrentJobMedia.szMediaType, (char*)pms_ptJob->tDefaultJobMedia.szMediaType);
  strcpy((char*)pstJob->tCurrentJobMedia.szMediaColor, (char*)pms_ptJob->tDefaultJobMedia.szMediaColor);
  pstJob->tCurrentJobMedia.uMediaWeight = pms_ptJob->tDefaultJobMedia.uMediaWeight ;
  pstJob->tCurrentJobMedia.dWidth = pms_ptJob->tDefaultJobMedia.dWidth;
  pstJob->tCurrentJobMedia.dHeight = pms_ptJob->tDefaultJobMedia.dHeight;
  pstJob->tCurrentJobMedia.pUser = NULL;
  pstJob->bAutoA4Letter = pms_ptJob->bAutoA4Letter;
  pstJob->dUserPaperWidth = pms_ptJob->dUserPaperWidth;
  pstJob->dUserPaperHeight = pms_ptJob->dUserPaperHeight;
  pstJob->bManualFeed = pms_ptJob->bManualFeed;
  pstJob->uPunchType = pms_ptJob->uPunchType;
  pstJob->uStapleType = pms_ptJob->uStapleType;

  pstJob->bDuplex = pms_ptJob->bDuplex;
  pstJob->bTumble = pms_ptJob->bTumble;
  pstJob->bCollate = pms_ptJob->bCollate;
  pstJob->bReversePageOrder = pms_ptJob->bReversePageOrder;

  pstJob->uBookletType = pms_ptJob->uBookletType;
  pstJob->uOhpMode = pms_ptJob->uOhpMode;
  pstJob->uOhpType = pms_ptJob->uOhpType;
  pstJob->uOhpInTray = pms_ptJob->uOhpInTray;
  pstJob->uCollatedCount = pms_ptJob->uCollatedCount;

  if( pms_ptJob->eRenderMode == PMS_RenderMode_Grayscale )
  {
    pstJob->eColorMode = OIL_Mono;
  }
  else
  {
    switch(pms_ptJob->eColorMode)  /* set mono or color */
    {
    case PMS_Mono:
      pstJob->eColorMode = OIL_Mono;
      break;
    case PMS_CMYK_Separations:
      pstJob->eColorMode = OIL_CMYK_Separations;
      break;
    case PMS_CMYK_Composite:
      pstJob->eColorMode = OIL_CMYK_Composite;
      break;
    case PMS_RGB_Separations:
      pstJob->eColorMode = OIL_RGB_Separations;
      break;
    case PMS_RGB_Composite:
      pstJob->eColorMode = OIL_RGB_Composite;
      break;
    case PMS_RGB_PixelInterleaved:
      pstJob->eColorMode = OIL_RGB_PixelInterleaved;
      break;
    default:
      HQFAILV(("CreateOILJob: Unsupported color mode request (%d), setting cmyk sep", pms_ptJob->eColorMode));
      pstJob->eColorMode = OIL_CMYK_Separations;     /* set to default - auto sep */
      break;
    }
  }

  /* For PCL5e jobs force to Mono 1bpp.
     This is optional, but it makes sense to output all PCL5e jobs using 
     1bpp Mono - anything else is wasteful. */
  if (ePDL == OIL_PDL_PCL5e) {
    g_ConfigurableFeatures.nDefaultColorMode = OIL_Mono;
    pstJob->uRIPDepth = 1;     /* 1bpp */
  } else {
    g_ConfigurableFeatures.nDefaultColorMode = pstJob->eColorMode;

    /* set the job bit depth from default PMS value */
    switch (pms_ptJob->eImageQuality)
    {
    case PMS_1BPP:
      pstJob->uRIPDepth = 1;     /* 1bpp */
      break;
    case PMS_2BPP:
      pstJob->uRIPDepth = 2;     /* 2bpp */
      break;
    case PMS_4BPP:
      pstJob->uRIPDepth = 4;     /* 4bpp */
      break;
    case PMS_8BPP_CONTONE:
      pstJob->uRIPDepth = 8;     /* 8bpp */
      break;
    case PMS_16BPP_CONTONE:
      pstJob->uRIPDepth = 16;     /* 16bpp */
      break;
    default:
      HQFAILV(("CreateOILJob: Unsupported bit depth request (%d), setting 1bpp", pms_ptJob->eImageQuality));
      pstJob->uRIPDepth = 1;     /* set 1 bpp */
      break;
    }
  }
  /* this function currently usese oil variable values, which is not ideal for a PMS callback !!!*/
  PMS_SetJobRIPConfig(ePDL, &(pstJob->uRIPDepth), &(g_ConfigurableFeatures.nDefaultColorMode));

  /* OutputDepth can can again if job overrides the RIP bit depth,
     so pass on the PMS setting to OIL. */
  pstJob->bOutputDepthMatchesRIP = pms_ptJob->bOutputBPPMatchesRIP;

  /* Should output match rendered bit depth */
  if(pms_ptJob->bOutputBPPMatchesRIP)
  {
    /* Set the job output bit depth to the RIP depth.
       Range validity is checked at time of conversion. */
    pstJob->uOutputDepth = pstJob->uRIPDepth;
  }
  else
  {
    /* Set the job output bit depth from default PMS value.
       Range validity is checked at time of conversion. */
    pstJob->uOutputDepth = pms_ptJob->uOutputBPP;
  }

  pstJob->bInputIsImage = pms_ptJob->bInputIsImage;
  strcpy(pstJob->szImageFile,pms_ptJob->szImageFile);

  switch(pms_ptJob->eScreenMode)  /* set screen mode */
  {
  case PMS_Scrn_Auto:
    pstJob->eScreenMode = OIL_Scrn_Auto;
    break;
  case PMS_Scrn_Photo:
    pstJob->eScreenMode = OIL_Scrn_Photo;
    break;
  case PMS_Scrn_Graphics:
    pstJob->eScreenMode = OIL_Scrn_Graphics;
    break;
  case PMS_Scrn_Text:
    pstJob->eScreenMode = OIL_Scrn_Text;
    break;
  case PMS_Scrn_ORIPDefault:
    pstJob->eScreenMode = OIL_Scrn_ORIPDefault;
    break;
  case PMS_Scrn_Job:
    pstJob->eScreenMode = OIL_Scrn_Job;
    break;
  case PMS_Scrn_Module:
    pstJob->eScreenMode = OIL_Scrn_Module;
    break;
  default:
    HQFAILV(("CreateOILJob: Unsupported screen mode request (%d), setting auto ", pms_ptJob->eScreenMode));
    pstJob->eScreenMode = OIL_Scrn_Auto;     /* set to default - auto */
    break;
  }

  switch(pms_ptJob->eTestPage)  /* set selected test page */
  {
  case PMS_TESTPAGE_CFG:
    pstJob->eTestPage = OIL_TESTPAGE_CFG;
    break;
  case PMS_TESTPAGE_PS:
    pstJob->eTestPage = OIL_TESTPAGE_PS;
    break;
  case PMS_TESTPAGE_PCL:
    pstJob->eTestPage = OIL_TESTPAGE_PCL;
    break;
  case PMS_TESTPAGE_NONE:
  default:
    pstJob->eTestPage = OIL_TESTPAGE_NONE;     /* set to NONE/off */
    break;
  }
  pstJob->bTestPagesComplete = FALSE;
  pstJob->uPrintErrorPage = pms_ptJob->uPrintErrorPage;
  pstJob->eImageScreenQuality = OIL_Scrn_LowLPI;
  pstJob->eGraphicsScreenQuality = OIL_Scrn_MediumLPI;
  pstJob->eTextScreenQuality = OIL_Scrn_HighLPI;

  pstJob->bSuppressBlank = pms_ptJob->bSuppressBlank;/* blank should be suppressed or pass through */ 
  pstJob->bPureBlackText = pms_ptJob->bPureBlackText;/* copy from default PMS values */ 
  pstJob->bAllTextBlack = pms_ptJob->bAllTextBlack;  /* copy from default PMS values */ 
  pstJob->bBlackSubstitute = pms_ptJob->bBlackSubstitute; /* copy from default PMS values */ 
  pstJob->bForceMonoIfCMYblank = pms_ptJob->bForceMonoIfCMYblank; /* copy from default PMS values */ 
  pstJob->uFontNumber = pms_ptJob->uFontNumber;
  strcpy(pstJob->szFontSource, pms_ptJob->szFontSource);
  pstJob->uLineTermination = pms_ptJob->uLineTermination;
  pstJob->dPitch = pms_ptJob->dPitch;
  pstJob->dPointSize = pms_ptJob->dPointSize;
  pstJob->uSymbolSet = pms_ptJob->uSymbolSet;
  pstJob->uPCL5PaperSize = MapPaperSize(OIL_PDL_PCL5c, pms_ptJob->tDefaultJobMedia.ePaperSize);
  pstJob->uPCLXLPaperSize = MapPaperSize(OIL_PDL_PCLXL, pms_ptJob->tDefaultJobMedia.ePaperSize);
  pstJob->uJobOffset = pms_ptJob->uJobOffset;
  pstJob->bCourierDark = pms_ptJob->bCourierDark;
  pstJob->bWideA4 = pms_ptJob->bWideA4;
  pstJob->dVMI = 48.0 / pms_ptJob->dLineSpacing;
  pstJob->bFileInput = pms_ptJob->bFileInput;
  strcpy(pstJob->szJobFilename, pms_ptJob->szJobFilename);
  strcpy(pstJob->szPDFPassword, pms_ptJob->szPDFPassword);
#ifdef USE_PJL
  pstJob->uJobStart = OIL_PjlGetJobStart();
  pstJob->uJobEnd = OIL_PjlGetJobEnd();
  pstJob->bEojReceived = FALSE;
  pstJob->szEojJobName[0] = '\0';
#endif

  pstJob->pPage = NULL;

  CreateOILJobNode(pstJob);
  if((g_ConfigurableFeatures.g_uGGShow & GG_SHOW_JOBCFG) != 0)
    dumpJOBconfig(pstJob);        /* debugging aid, display initial job configuration */
  return pstJob;
}

/**
 * \brief Internal function which appends the supplied job node to OIL's job list.
 *
 * This function accepts a pointer to an OIL job structure and adds it to the list of OIL jobs.
 * It is not exposed in oil_job_handler.h.
 * \param[in]   pstJob     A pointer to an OIL job.
 */
static void CreateOILJobNode(OIL_TyJob *pstJob)
{
  OIL_TyJobList **pstLastNode, *pstNewNode;

  pstNewNode = (OIL_TyJobList*)OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, sizeof(OIL_TyJobList));
  HQASSERTV(pstNewNode!=NULL,
            ("CreateOILJobNode: Failed to allocate %lu bytes", (unsigned long) sizeof(OIL_TyJobList)));
  pstNewNode->pstJob = pstJob;
  pstNewNode->pNext = NULL;

  pstLastNode = &l_pstOilJobList;
  while(*pstLastNode != NULL)
    pstLastNode = &(*pstLastNode)->pNext;

  *pstLastNode = pstNewNode;
}

/**
 * \brief Internal function which removes the specified job from the OIL's job list.
 *
 * This function accepts a job ID. It searches for a job with that ID in the job
 * list, and removes it from the list.  This function is not exposed in oil_job_handler.h.
 * \param[in]   JobID     The unique ID of the job to be removed from the list.
 */
static void DeleteOILJobNode(unsigned int JobID)
{
  OIL_TyJobList **ppstHead, *pstNodeToDelete;
    
  ppstHead = &l_pstOilJobList;

  while(*ppstHead)
  {
    pstNodeToDelete = *ppstHead;
    if(pstNodeToDelete->pstJob->uJobId == JobID)
    {
      *ppstHead = (*ppstHead)->pNext;
      OIL_free(OILMemoryPoolJob, pstNodeToDelete);
      return;
    }
    ppstHead = &(*ppstHead)->pNext;
  }
  HQFAILV(("DeleteOilJob: Job ID %d did not match any of the active jobs", JobID));
}

/**
 * \brief Retrieve a pointer to a job in the OIL's job list, identified by job ID.
 *
 * This function accepts a job ID. It searches for a job with that ID in the job
 * list, and returns a pointer to it.
 * \param[in]   JobID     The unique ID of the job to be found in the list.
 */
OIL_TyJob* GetJobByJobID(unsigned int JobID)
{
  OIL_TyJobList **ppstNextNode;
    
  ppstNextNode = &l_pstOilJobList;

  while(*ppstNextNode)
  {
    if((*ppstNextNode)->pstJob->uJobId == JobID)
    {
      return (*ppstNextNode)->pstJob;
    }
    ppstNextNode = &(*ppstNextNode)->pNext;
  }
  return NULL;
}

/**
 * \brief Delete a job from the OIL's job list, identified by job ID.
 *
 * This function accepts a job ID. It searches for a job with that ID in the job
 * list, removes it from the list and deletes the job, freeing the memory that was
 * allocated to it.
 * \param[in]   JobID     The unique ID of the job to be deleted.
 */
void DeleteOILJob(unsigned int JobID)
{
  OIL_TyJob *ptOILJob;

  ptOILJob = GetJobByJobID(JobID);
  if(ptOILJob == NULL)
  {
    HQFAIL("DeleteOILJob() ptOILJob is NULL");
    return;
  }

  ebddev_ClearJobParams();

#ifdef USE_PJL
  OIL_PjlReportEoj(ptOILJob);
#endif

  DeleteOILJobNode(JobID);
  OIL_free(OILMemoryPoolJob, ptOILJob);
}

/**
 * \brief Internal function which maps a PMS paper size indicator to an OIL one.
 *
 * This function accepts an integer which represents a paper size in the 
 * PMS code and converts it to a paper tray designation for use by the OIL. This is only
 * necessary for jobs using some variant of PCL. If the input paper size is not recognised, 
 * an error message is generated and output defaults to custom paper size for PCL 5 jobs, 
 * and the default paper size for PCL-XL jobs.
 *
 * This function is not exposed in oil_job_handler.h. 
 * \param[in]   ePDL               An integer representing the PDL; expected to be one of the values 
                                   defined by OIL_eTyPDLType.
 * \param[in]   ePMS_PaperSize     An integer representing an output tray known to the PMS.
 * \return      An integer representing a PCL paper size.
 */
static unsigned int MapPaperSize(int ePDL, unsigned int ePMS_PaperSize)
{
  unsigned int ePCL_PaperSize;
  PMS_TyPaperInfo *pPaperInfo;

  if( ePDL == OIL_PDL_PCL5c || ePDL == OIL_PDL_PCL5e )
  {
    /* PMS_GetPaperInfo returns a PMS_SIZE_DONT_KNOW paper size info pointer if function fails */
    if(!PMS_GetPaperInfo(ePMS_PaperSize,&pPaperInfo))
      HQFAILV(("MapPaperSize: Unknown paper size: %d", ePMS_PaperSize));
    ePCL_PaperSize = pPaperInfo->ePCL5size;
  }
  else if( ePDL == OIL_PDL_PCLXL )
  {
    if(!PMS_GetPaperInfo(ePMS_PaperSize,&pPaperInfo))
      HQFAILV(("MapPaperSize: Unknown paper size: %d", ePMS_PaperSize));
    ePCL_PaperSize = pPaperInfo->ePCLXLsize;
  }
  else
  {
    /* Not a PCL job, so not used */
    ePCL_PaperSize = 0;
  }

  return ePCL_PaperSize;
}

/**
 * \brief Internal function which outputs the job configuration when -z 7 option selected.
 *
 * This function displays a list of job configuration items at the start of each PDL processing cycle
 * for the job to provide confirmation of the intial job setup.
 *
 * This function is not exposed in oil_job_handler.h. 
 * \param[in]   none 
 * \return      nothing.
 */
static void dumpJOBconfig(OIL_TyJob *pstJob)
{
  int temp, temp1;
  /* This table relies on the values defined in oil.h for PDL Type enums */
  char bufPDL[8][8] = {"Unknown", "PS", "XPS", "PDF", "PCL 5c", "PCL 5e", "PCL XL", "Image"};
  char bufColor[7][10] = {"Invalid", "Mono", "CMYK Sep", "CMYK Comp", "RGB Sep", "RGB Comp", "RGB Comp"};

  if(pstJob->ePDLType <= 7)
    temp = pstJob->ePDLType;
  else
    temp = 0;
  if(g_ConfigurableFeatures.nDefaultColorMode <= 6)
    temp1 = g_ConfigurableFeatures.nDefaultColorMode;
  else
    temp1 = 0;

  GG_SHOW(GG_SHOW_JOBCFG, "  Memory %dMB, PDL %s, Color Mode %s, %s Interleaving, BPP %d, Resolution %d x %d\n",
                        g_tSystemInfo.cbRIPMemory/(1024*1024), bufPDL[temp], bufColor[temp1],
                          ((g_ConfigurableFeatures.bPixelInterleaving || g_ConfigurableFeatures.nDefaultColorMode == 6)? "Pixel":"Band"),
                            pstJob->uRIPDepth, pstJob->uXResolution, pstJob->uYResolution);

  GG_SHOW(GG_SHOW_JOBCFG, "  CMM %s, Screen Mode %d, Output Mode %d, Media Mode %d, Input Mode %s\n",
                      (g_ConfigurableFeatures.uColorManagement? "GG Profile":"Off"),
                            pstJob->eScreenMode, g_tSystemInfo.eBandDeliveryType,
                              g_ConfigurableFeatures.g_ePaperSelectMode, (g_SocketInPort? "Socket":"File"));
}

