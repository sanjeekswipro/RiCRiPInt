/* Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_engine_simulator.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Engine Simulator.
 *
 */

#include "pms.h"
#include "pms_engine_simulator.h"
#include "pms_interface_oil2pms.h"
#include "pms_malloc.h"
#include "pms_platform.h"
#include "pms_page_handler.h"
#include "oil_entry.h"

#include <string.h> /* for strcpy */


PMS_TyJob g_tJob;

/**
 * \brief Simulates the output
 *
 * This routine simulates the movement of paper in the engine.
 * The first part of the loop determines if a sheet can be lifted from a paper tray,
 * the second part determines if the sheet has reached the output tray.
 */
void PMSOutput(void * dummy)
{
  PMS_TyPage *ThisPage = NULL;    /* pointer the the current page being checked */
  PMS_TyPage LastPage;            /* pointer the the last sheet that was picked up */
  PMS_TyPageList *tmpPageList;    /* local pagelist pointer */
  unsigned int uCurrentPageGap;   /* the gap between sheet pickup */
  unsigned int uLastPickUp;       /* the time the last sheet was pickup from inout tray */
  unsigned int  uPagesInMotion;   /* the number of sheets travelling in the system */

  UNUSED_PARAM(void *, dummy);

  uCurrentPageGap = 0;
  uLastPickUp = 0;
  uPagesInMotion = 0;
  g_eJobState = PMS_Waiting_For_Page;

  /* loop until the RIP has quit */
  while(g_eJobState != PMS_AllJobs_Completed)  /* pages in motion OR RIP active */
  {
    /* keep a local copy of the pagelist for local manipulation */
    tmpPageList = g_pstPageList;

    if(g_tSystemInfo.uUseEngineSimulator)
    {
      /* small delay to prevent total processor consumption */
      PMS_Delay(PAGECHECK_FREQUENCY);
    }
    else if(tmpPageList==NULL)
    {
      /* wait for a page to arrive */
      PMS_WaitOnSemaphore_Forever(g_semPageQueue);
    }

    /* walk through all the pages */
    while (tmpPageList!=NULL)
    {
      ThisPage = tmpPageList->pPage;
      if(g_tSystemInfo.uUseEngineSimulator)
        PMS_Delay(PAGECHECK_FREQUENCY);

      /* is there a page waiting to start travelling */
      if (ThisPage->eState==PMS_CHECKEDIN)
      {
        /* work out the gap between last page and this page */
        uCurrentPageGap = EngineGetInterpageGap(ThisPage, &LastPage);

        /* has enough time passed since the last sheet pickup */
        if(PMS_TimeInMilliSecs() > (uLastPickUp + uCurrentPageGap))
        {
          /* pick up sheet, mark as printing */
          ThisPage->eState = PMS_PRINTING;
          ThisPage->ulPickupTime = PMS_TimeInMilliSecs();
          ThisPage->ulOutputTime = ThisPage->ulPickupTime + EngineGetTravelTime(g_pstPageList->pPage);
          uPagesInMotion++;

          /* PMS_SHOW("Pickup Sheet: %d - output: %d\n", ThisPage->ulPickupTime, ThisPage->ulOutputTime); */

          /* record some stats about this page incase next page is different a requires a stall in paper path */
          LastPage.uTotalPlanes = ThisPage->uTotalPlanes;

          /* record pickup time in global */
          uLastPickUp = PMS_TimeInMilliSecs();
        }
        break;
      }

      /* check sheets that are printing */
      if (ThisPage->eState==PMS_PRINTING)
      {
        /* has a sheet reached output bin */
        if(PMS_TimeInMilliSecs() > (ThisPage->ulOutputTime))
        {
          /* print sheet */
          PrintPage(ThisPage);
          ThisPage->eState = PMS_COMPLETE;

          /* PMS_SHOW("Output Sheet: %d\n", PMS_TimeInMilliSecs()); */

          /* send page done at this point*/
          OIL_PageDone(ThisPage);

          /* remove sheet from system, RemovePage is protected against other threads */
          RemovePage(ThisPage);
          uPagesInMotion--;

          /* page list modified so break out and start again */
          break;
        }
      }

      /* move onto the next page */
      tmpPageList = tmpPageList->pNext;
    }

    if(uPagesInMotion == 0)
    {
        /* set the job state to indicate pms is waiting for next page. */
        g_eJobState = PMS_Waiting_For_Page;
    }
    else
    {
        /* set the job state to indicate pms has received a page and is processing it. */
        g_eJobState = PMS_Page_In_Progress;
    }

    /* if RIP has finished ripping and page list is empty means all jobs are printed */
    if((g_eRipState == PMS_Rip_Finished) && (g_pstPageList == NULL))
    {
      g_eJobState = PMS_AllJobs_Completed;
    }
  }
  return;
}

/**
 * \brief Calculates the gap before next sheet can be picked up.
 *
 * This routine calculates the wait before the next page can be picked up, based on job type and engein speed.\n
 */
int EngineGetInterpageGap(PMS_TyPage *pstThisPage, PMS_TyPage *pstLastPage)
{
  int uPageDelay = 0;

  UNUSED_PARAM(PMS_TyPage, pstLastPage);

  if(g_tSystemInfo.uUseEngineSimulator == TRUE)
  {
    /* TODO - stall paper path */
    /* check last page and this page to determine the gap required */
    if (pstThisPage->uTotalPlanes == 1)
    {
      /* mono page */
      uPageDelay = INTERPAGE_GAP(MONO_PLAIN_PAPER_PPM);
    }
    if (pstThisPage->uTotalPlanes == 4)
    {
      /* color page */
      uPageDelay = INTERPAGE_GAP(COLOR_PLAIN_PAPER_PPM);
    }
  }
  else
  {
    uPageDelay = 0;
  }

  return (uPageDelay);
}

/**
 * \brief Calculates the time required to process the page.
 *
 * This routine calculates the approximate time required to process the page
 * depending on different parameters of the page.\n
 */
int EngineGetTravelTime(PMS_TyPage *pstPMSPage)
{
  int nPrintTime = 0;
  if(g_tSystemInfo.uUseEngineSimulator == TRUE)
  {
    switch(pstPMSPage->stMedia.uInputTray)
    {
    case PMS_TRAY_AUTO:
    case PMS_TRAY_BYPASS:
    case PMS_TRAY_MANUALFEED:
    case PMS_TRAY_TRAY1:
    case PMS_TRAY_TRAY2:
    case PMS_TRAY_TRAY3:
    case PMS_TRAY_ENVELOPE:
    default:
        nPrintTime += INPUT_TRAY1_DELAY;
        break;
    }

    switch(pstPMSPage->stMedia.eOutputTray)
    {
    case PMS_OUTPUT_TRAY_AUTO:
    case PMS_OUTPUT_TRAY_UPPER:
    case PMS_OUTPUT_TRAY_LOWER:
    case PMS_OUTPUT_TRAY_EXTRA:
    default:
        nPrintTime += OUTPUT_TRAY1_DELAY;
        break;
    }
    nPrintTime += PAPERPATH_DELAY;
  }
  return (nPrintTime);
}

/**
 * \brief Fill Tray Information.
 *
 * This routine simulates sensing of page attributes from available media sources.  \n
 */
int EngineGetTrayInfo(void)
{
  int nTrayIndex;

  /* query engine and fill in information about available attributes 
   * NOTE - maximum of 10 trays
  */
  g_pstTrayInfo = (PMS_TyTrayInfo *) OSMalloc( NUMFIXEDMEDIASOURCES * sizeof(PMS_TyTrayInfo), PMS_MemoryPoolPMS );
  nTrayIndex = 0;

  if( g_pstTrayInfo != NULL )
  {
    g_pstTrayInfo[nTrayIndex].eMediaSource = PMS_TRAY_MANUALFEED;
    g_pstTrayInfo[nTrayIndex].ePaperSize = PMS_SIZE_A4;
    g_pstTrayInfo[nTrayIndex].eMediaType = PMS_TYPE_DONT_KNOW;
    g_pstTrayInfo[nTrayIndex].eMediaColor = PMS_COLOR_DONT_KNOW;
    g_pstTrayInfo[nTrayIndex].uMediaWeight = 0;
    g_pstTrayInfo[nTrayIndex].nPriority = 1;
    g_pstTrayInfo[nTrayIndex].bTrayEmptyFlag = FALSE;
    g_pstTrayInfo[nTrayIndex].nNoOfSheets = 200;
    nTrayIndex++;

    g_pstTrayInfo[nTrayIndex].eMediaSource = PMS_TRAY_TRAY1;
    g_pstTrayInfo[nTrayIndex].ePaperSize = PMS_SIZE_A4_R;
    g_pstTrayInfo[nTrayIndex].eMediaType = PMS_TYPE_DONT_KNOW;
    g_pstTrayInfo[nTrayIndex].eMediaColor = PMS_COLOR_DONT_KNOW;
    g_pstTrayInfo[nTrayIndex].uMediaWeight = 0;
    g_pstTrayInfo[nTrayIndex].nPriority = 2;
    g_pstTrayInfo[nTrayIndex].bTrayEmptyFlag = FALSE;
    g_pstTrayInfo[nTrayIndex].nNoOfSheets = 200;
    nTrayIndex++;

    g_pstTrayInfo[nTrayIndex].eMediaSource = PMS_TRAY_TRAY2;
    g_pstTrayInfo[nTrayIndex].ePaperSize = PMS_SIZE_LETTER;
    g_pstTrayInfo[nTrayIndex].eMediaType = PMS_TYPE_DONT_KNOW;
    g_pstTrayInfo[nTrayIndex].eMediaColor = PMS_COLOR_DONT_KNOW;
    g_pstTrayInfo[nTrayIndex].uMediaWeight = 0;
    g_pstTrayInfo[nTrayIndex].nPriority = 3;
    g_pstTrayInfo[nTrayIndex].bTrayEmptyFlag = FALSE;
    g_pstTrayInfo[nTrayIndex].nNoOfSheets = 200;
    nTrayIndex++;

    return nTrayIndex;
  }

  return 0;
}

/**
 * \brief Fill Output Tray Information.
 *
 * This routine simulates sensing of page attributes from available media destinations.  \n
 */
int EngineGetOutputInfo(void)
{
  int nTrayIndex;

  /* query engine and fill in information about available attributes 
   * NOTE - maximum of 10 trays
  */
  g_pstOutputInfo = (PMS_TyOutputInfo *) OSMalloc( NUMFIXEDMEDIADESTS * sizeof(PMS_TyOutputInfo), PMS_MemoryPoolPMS );
  nTrayIndex = 0;

  if( g_pstOutputInfo != NULL )
  {
    g_pstOutputInfo[nTrayIndex].eOutputTray = PMS_OUTPUT_TRAY_UPPER;
    g_pstOutputInfo[nTrayIndex].nPriority = 1;
    nTrayIndex++;

    g_pstOutputInfo[nTrayIndex].eOutputTray = PMS_OUTPUT_TRAY_LOWER;
    g_pstOutputInfo[nTrayIndex].nPriority = 2;
    nTrayIndex++;
    return nTrayIndex;
  }

  return 0;
}
/**
 * \brief Fill job information.
 *
 * This routine simulates reading the job settings from NVRAM.  \n
 */
PMS_TyJob * EngineGetJobSettings(void)
{
  /* Just returns the factory defaults in the absence of any persistent settings */
  EngineSetJobDefaults();

  return &g_tJob;
}

/**
 * \brief Fill job default information.
 *
 * This routine simulates reverting the job settings to factory defaults.  \n
 */
void EngineSetJobDefaults(void)
{
  PMS_TyPaperInfo * pPaperInfo = NULL;

  g_tJob.uJobId = 0;                        /* Job ID number */
  strcpy(g_tJob.szHostName, "Immortal");    /* Printer hostname */
  strcpy(g_tJob.szUserName, "Scott");       /* Job user name */
  g_tJob.szJobName[0] = '\0';               /* Job name */
  g_tJob.szPjlJobName[0] = '\0';            /* PJL Job name */
  g_tJob.uCopyCount = 1;                    /* Total copies to print */
  g_tJob.uXResolution = g_tSystemInfo.uDefaultResX; /* x resolution */
  g_tJob.uYResolution = g_tSystemInfo.uDefaultResY; /* y resolution */
  g_tJob.uOrientation = 0;                  /* orientation, 0-portrait or 1-landscape */

  g_tJob.tDefaultJobMedia.ePaperSize = PMS_SIZE_A4;   /* A4*/
  g_tJob.tDefaultJobMedia.uInputTray = PMS_TRAY_AUTO;            /* media source selection */
  g_tJob.tDefaultJobMedia.eOutputTray = PMS_OUTPUT_TRAY_AUTO;   /* selected output tray */
  strcpy((char*)g_tJob.tDefaultJobMedia.szMediaType, "");
  strcpy((char*)g_tJob.tDefaultJobMedia.szMediaColor, "");
  g_tJob.tDefaultJobMedia.uMediaWeight = 0;
  PMS_GetPaperInfo(g_tJob.tDefaultJobMedia.ePaperSize, &pPaperInfo);
  g_tJob.tDefaultJobMedia.dWidth = pPaperInfo->dWidth;
  g_tJob.tDefaultJobMedia.dHeight = pPaperInfo->dHeight;

  g_tJob.bAutoA4Letter = FALSE;   /* Automatic A4/letter switching */
  /* get CUSTOM_PAPER size to initialise job structure (set from PJL) */
  PMS_GetPaperInfo(PMS_SIZE_CUSTOM, &pPaperInfo);
  g_tJob.dUserPaperWidth = pPaperInfo->dWidth;   /* User defined paper width */
  g_tJob.dUserPaperHeight = pPaperInfo->dHeight ;  /* User defined paper height */
  g_tJob.bManualFeed = FALSE;     /* Manual feed */
  g_tJob.uPunchType = 1;          /* Punch type, single double etc */
  g_tJob.uStapleType = 1;         /* staple operation, 1 hole, 2 hole, centre etc */
  g_tJob.bDuplex = FALSE;         /* true = duplex, false = simplex */
  g_tJob.bTumble = FALSE;         /* true = tumble, false = no tumble */
  g_tJob.bCollate = FALSE;        /* true = collate, false = no collate */
  g_tJob.bReversePageOrder = FALSE;   /* Reverse page order */
  g_tJob.uBookletType = 1;        /* booklet binding, left, right */
  g_tJob.uOhpMode = 1;            /* OHP interleaving mode */
  g_tJob.uOhpType = 1;            /* OHP interleaving media type */
  g_tJob.uOhpInTray = 1;          /* OHP interleaving feed tray */
  g_tJob.uCollatedCount = 1;      /* Total collated copies in a job */
  g_tJob.eImageQuality = g_tSystemInfo.eImageQuality;   /* ImageQuality - 1bpp, 2bpp etc */
  g_tJob.bOutputBPPMatchesRIP = g_tSystemInfo.bOutputBPPMatchesRIP; /* Output bit depth. */
  g_tJob.uOutputBPP = g_tSystemInfo.uOutputBPP;         /* Output bit depth. */
  g_tJob.eColorMode = g_tSystemInfo.eDefaultColMode;    /* 1=Mono; 2=SeparationsCMYK; 3=CompositeCMYK; 4=SeparationsRGB; 5=CompositeRGB; */
  g_tJob.bForceMonoIfCMYblank = g_tSystemInfo.bForceMonoIfCMYblank; /* true = force mono if cmy absent, false = output all 4 planes */
  g_tJob.eScreenMode = g_tSystemInfo.eDefaultScreenMode;/* Screening type */
  g_tJob.bSuppressBlank = TRUE;  /* true = suppress blank pages, false = let blank pages pass through */
  g_tJob.bPureBlackText = FALSE;  /* true = Pure Black Text enabled */
  g_tJob.bAllTextBlack = FALSE;   /* true = All Text Black Enabled */
  g_tJob.bBlackSubstitute = FALSE; /* true = Black Substitute enabled */

  g_tJob.uFontNumber = 0;
  strcpy(g_tJob.szFontSource, "I");
  g_tJob.uLineTermination = 0;
  g_tJob.dPitch = 10.0;
  g_tJob.dPointSize = 12.0;
  g_tJob.uSymbolSet = 277;        /* 8U - Roman-8 */

  g_tJob.dLineSpacing = 6.0;
  g_tJob.eRenderMode = PMS_RenderMode_Color;
  g_tJob.eRenderModel = PMS_RenderModel_CMYK8B;
  g_tJob.uJobOffset = 0;
  g_tJob.bCourierDark = FALSE;
  g_tJob.bWideA4 = FALSE;
  g_tJob.bInputIsImage = FALSE; 
  g_tJob.szImageFile[0] = '\0';   /* Image File */
  g_tJob.eTestPage = PMS_TESTPAGE_NONE;   /* Test Page */
  g_tJob.uPrintErrorPage = 0;   /* Print Error Page is off */
  g_tJob.bFileInput = g_tSystemInfo.bFileInput; /* true = job input from file */
  g_tJob.szJobFilename[0] = '\0';   
  strcpy(g_tJob.szPDFPassword, "thassos");
  g_tNextSystemInfo=g_tSystemInfo;
}

