/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_interface_oil2pms.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief     This file implements the functions that support communication
 *   between the OIL and the PMS.
 *
 */


#include "oil.h"
#include "oil_media.h"
#include "pms_export.h"
#include "oil_malloc.h"
#include "oil_interface_oil2pms.h"
#include "oil_ebddev.h"
#include "oil_interface_skin2oil.h"
#include "oil_page_handler.h"
#include "oil_probelog.h"
#ifdef USE_PJL
#include "oil_pjl.h"
#endif

/* swdevice needed for embedded device file descriptors */
#include "swdevice.h"

#include <string.h>
#include <stdio.h>      /* for printf in assert */
#include <math.h>

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;
extern OIL_TyPage *g_pstCurrentPage;
extern OIL_TySystem g_SystemState;
extern int g_bPageChecksum;

extern OIL_TyJob* GetJobByJobID(unsigned int JobID);
extern OIL_TyError g_JobErrorData;

int(**g_apfn_pms_calls[PMS_TOTAL_NUMBER_OF_API_FUNCTIONS])();

/**
 * \brief Pass a formatted message to the PMS.
 *
 * This function accepts a string and an optional set of arguments
 * to that string. It uses the string and any arguments to create a 
 * formatted string, which is then passed to the PMS.
 * \param[in] str   A string, optionally containing formatting tags.
 * \param[in] ...   An optional list of arguments to the string.
 */
void oil_printf(char *str,...)
{
  char buf[1024];
  va_list args;
  va_start(args, str);
  vsprintf(buf,str,args);
  HQASSERTV((strlen(buf)<sizeof(buf)), ("oil_printf buffer overrun %d, %d", strlen(buf),sizeof(buf)) );
  PMS_PutDebugMessage(buf);
  va_end(args);
}

/**
 * \brief Create a PMS page structure from an OIL page.
 *
 * An OIL page structure is passed in and a PMS page strucutre is returned.
 *
 * This function allocates memory for the PMS page and populates the PMS
 * page structure with data from the OIL page structure.\n
 * The PMS page points to plane and band data stored in the OIL page so
 * the OIL page structure must not be destroyed until the entire page has been output
 * (as notified by OIL_PageDone()).
 * \param[in]   ptOILPage   A pointer to an OIL_TyPage structure.
 * \return      A pointer to a PMS page, built from the OIL page supplied.
 */
PMS_TyPage* CreatePMSPage(OIL_TyPage *ptOILPage)
{
  unsigned int i, j;
  PMS_TyPage *ptPMSPage;

  OIL_ProbeLog(SW_TRACE_OIL_CREATEPMSPAGE, SW_TRACETYPE_ENTER, (intptr_t)0);

  /* assert if no of bands is greater than pms max limit */
  HQASSERTV(ptOILPage->atPlane[3].uBandTotal < PMS_BAND_LIMIT,
            ("CreatePMSPage: Exceeded the limit of maximum bands (%d)", PMS_BAND_LIMIT));

  /* allocate the memory */
  ptPMSPage = (PMS_TyPage *)OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, sizeof(PMS_TyPage));
  HQASSERTV(ptPMSPage!=NULL,
            ("CreatePMSPage: Failed to allocate %lu bytes", (unsigned long) sizeof(PMS_TyPage)));

  /* fill in the PMS page data */
  ptPMSPage->JobId = ptOILPage->pstJob->uJobId;
  ptPMSPage->pJobPMSData = (PMS_TyJob *)(ptOILPage->pstJob->pJobPMSData);
  strcpy(ptPMSPage->szJobName, ptOILPage->pstJob->szJobName);
  ptPMSPage->PageId = ptOILPage->uPageNo;
  ptPMSPage->nPageWidthPixels = ptOILPage->nPageWidthPixels;
  ptPMSPage->nPageHeightPixels = ptOILPage->nPageHeightPixels;
  ptPMSPage->nRasterWidthBits = ptOILPage->nRasterWidthData;
  if(ptOILPage->pstJob->eColorMode == OIL_RGB_PixelInterleaved) {
    ptPMSPage->nRasterWidthPixels = (ptOILPage->nRasterWidthData / ptOILPage->uOutputDepth) / 3;
  } else {
    ptPMSPage->nRasterWidthPixels = (ptOILPage->nRasterWidthData / ptOILPage->uOutputDepth);
  }
  ptPMSPage->dXResolution = ptOILPage->dXRes;
  ptPMSPage->dYResolution = ptOILPage->dYRes;
  ptPMSPage->uRIPDepth = ptOILPage->uRIPDepth;
  ptPMSPage->uOutputDepth = ptOILPage->uOutputDepth;
  ptPMSPage->nTopMargin = ptOILPage->nTopMargin;
  ptPMSPage->nBottomMargin = ptOILPage->nBottomMargin;
  ptPMSPage->nLeftMargin = ptOILPage->nLeftMargin;
  ptPMSPage->nRightMargin = ptOILPage->nRightMargin;
  ptPMSPage->eColorantFamily = ptOILPage->eColorantFamily;
  ptPMSPage->bForceMonoIfCMYblank = ptOILPage->pstJob->bForceMonoIfCMYblank;
  ptPMSPage->uTotalPlanes = ptOILPage->nColorants;
  for(i=0; i < PMS_MAX_PLANES_COUNT; i++)
  {
    ptPMSPage->atPlane[i].uBandTotal = ptOILPage->atPlane[i].uBandTotal;
    switch(ptOILPage->atPlane[i].ePlaneColorant)
    {
    case OIL_Cyan:
      ptPMSPage->atPlane[i].ePlaneColorant = PMS_CYAN;
      break;
    case OIL_Magenta:
      ptPMSPage->atPlane[i].ePlaneColorant = PMS_MAGENTA;
      break;
    case OIL_Yellow:
      ptPMSPage->atPlane[i].ePlaneColorant = PMS_YELLOW;
      break;
    case OIL_Black:
      ptPMSPage->atPlane[i].ePlaneColorant = PMS_BLACK;
      break;
    case OIL_Red:
      ptPMSPage->atPlane[i].ePlaneColorant = PMS_RED;
      break;
    case OIL_Green:
      ptPMSPage->atPlane[i].ePlaneColorant = PMS_GREEN;
      break;
    case OIL_Blue:
      ptPMSPage->atPlane[i].ePlaneColorant = PMS_BLUE;
      break;
    case OIL_InvalidColor:
    default:
      ptPMSPage->atPlane[i].ePlaneColorant = PMS_INVALID_COLOURANT;
      break;
    }
    /* Don't pass values of bBlankPlane from OIL page to the PMS one,
       since they're all TRUE in the case of PUSH_BAND and
       PUSH_BAND_DIRECT because the page is passed early to the PMS,
       before any bands have arrived. Instead, default it to FALSE
       here and let CreatePMSBlankPlane set it to TRUE when
       necessary. */
    ptPMSPage->atPlane[i].bBlankPlane = FALSE;
    for(j=0; j < ptOILPage->atPlane[i].uBandTotal; j++)
    {
      ptPMSPage->atPlane[i].atBand[j].uBandHeight = ptOILPage->atPlane[i].atBand[j].uBandHeight;
      ptPMSPage->atPlane[i].atBand[j].uBandNumber = ptOILPage->atPlane[i].atBand[j].uBandNumber;
      ptPMSPage->atPlane[i].atBand[j].pBandRaster = ptOILPage->atPlane[i].atBand[j].pBandRaster;
      ptPMSPage->atPlane[i].atBand[j].cbBandSize  = ptOILPage->atPlane[i].atBand[j].cbBandSize;
    }
    for(; j < PMS_BAND_LIMIT; j++)
    {
      ptPMSPage->atPlane[i].atBand[j].uBandHeight = 0;
      ptPMSPage->atPlane[i].atBand[j].uBandNumber = 0;
      ptPMSPage->atPlane[i].atBand[j].pBandRaster = NULL;
      ptPMSPage->atPlane[i].atBand[j].cbBandSize  = 0;
    }
  }
  ptPMSPage->nCopies = ptOILPage->uCopies;
  ptPMSPage->stMedia.uInputTray = ptOILPage->stMedia.uInputTray;
  ptPMSPage->stMedia.eOutputTray = ptOILPage->stMedia.uOutputTray;
  ptPMSPage->bIsRLE = ptOILPage->bIsRLE;
  ptPMSPage->bDuplex = ptOILPage->bDuplex;
  ptPMSPage->bCollate = ptOILPage->bCollate;
  ptPMSPage->bTumble = ptOILPage->bTumble;
  ptPMSPage->uOrientation = ptOILPage->uOrientation;
  ptPMSPage->bFaceUp = ptOILPage->bFaceUp;
  ptPMSPage->eColorMode = ptOILPage->pstJob->eColorMode; /* being used only in tiff comment field */
  ptPMSPage->eScreenMode = ptOILPage->pstJob->eScreenMode; /* being used only in tiff comment field */
  ptPMSPage->ulPickupTime = 0;
  ptPMSPage->ulOutputTime = 0;
  ptPMSPage->eState = PMS_CREATED;
  ptPMSPage->nBlankPage = ptOILPage->nBlankPage;
  ptPMSPage->stMedia.dWidth = ptOILPage->stMedia.dWidth;
  ptPMSPage->stMedia.dHeight = ptOILPage->stMedia.dHeight;
  strcpy((char*)ptPMSPage->stMedia.szMediaType, (char*)ptOILPage->stMedia.szMediaType);
  strcpy((char*)ptPMSPage->stMedia.szMediaColor, (char*)ptOILPage->stMedia.szMediaColor);
  ptPMSPage->stMedia.uMediaWeight = ptOILPage->stMedia.uMediaWeight;
  {
    /* discover paper size by asking PMS for trays and checking for the tray number 
    that matches the tray enum value, as this is used to set the InputAttributes value
    ** Note this does not relate directly to the tray number ** needs fixing??  TODO**/
    PMS_TyTrayInfo *pstPMSTrays = NULL;
    unsigned int nTrays = PMS_GetTrayInfo(&pstPMSTrays);
    ptPMSPage->stMedia.ePaperSize = pstPMSTrays[0].ePaperSize;
    for(j = 0; j < nTrays; j++)
    {
       if(pstPMSTrays[j].eMediaSource == (int)(ptPMSPage->stMedia.uInputTray))
       {
          ptPMSPage->stMedia.ePaperSize = pstPMSTrays[j].ePaperSize;
          break;
       }
    }
  }

  OIL_ProbeLog(SW_TRACE_OIL_CREATEPMSPAGE, SW_TRACETYPE_EXIT, (intptr_t)0);

  return ptPMSPage;
}

/**
 * \brief Delete a PMS page structure.
 *
 * The PMS page structure passed is deleted. All resources associated with 
 * the PMS page structure are freed
 * \param       ptPMSPage   A pointer to the page to be deleted.
 */
void DeletePMSPage(PMS_TyPage *ptPMSPage)
{
  OIL_ProbeLog(SW_TRACE_OIL_DELETEPMSPAGE, SW_TRACETYPE_ENTER, (intptr_t)0);
  /* remove any raster data created in PMS for j 2 and j 3 mode*/
  if (g_ConfigurableFeatures.eBandDeliveryType == OIL_PUSH_BAND ||
      g_ConfigurableFeatures.eBandDeliveryType == OIL_PUSH_BAND_DIRECT_SINGLE)
  {
      PMS_DeletePrintedPage (ptPMSPage);
  }
  OIL_free(OILMemoryPoolJob, ptPMSPage); /*free the PMS page memory*/
  ptPMSPage = NULL;
  OIL_ProbeLog(SW_TRACE_OIL_DELETEPMSPAGE, SW_TRACETYPE_EXIT, (intptr_t)0);
}

/**
 * \brief Callback to indicate that a page has been completely processed by the PMS.
 *
 * This is called by the PMS to notify the OIL that the page has been completely
 * output and all resources used by this page can be freed.\n
 * The corresponding OIL page is deleted from the OIL's list of pages as well as
 * the PMS page structure which was created by OIL in CreatePMSPage().
 *
 */
void OIL_PageDone(PMS_TyPage *ptPMSPage)
{
  OIL_ProbeLog(SW_TRACE_OIL_PAGEDONE, SW_TRACETYPE_ENTER, (intptr_t)0);
  GG_SHOW(GG_SHOW_OIL, "Received PAGEDONE: JobID: %d, PageID: %d \n ", ptPMSPage->JobId, ptPMSPage->PageId);
  GGglobal_timing(SW_TRACE_OIL_PAGEDONE, ptPMSPage->PageId);

  /* free the OIL memory associated with the page */
  ProcessPageDone(ptPMSPage->JobId, ptPMSPage->PageId);

  /* free the PMS memory associated with the page */
  DeletePMSPage(ptPMSPage);
  OIL_ProbeLog(SW_TRACE_OIL_PAGEDONE, SW_TRACETYPE_EXIT, (intptr_t)0);
}

/**
 * \brief Callback to enable the PMS to retrieve a band from the OIL.
 *
 * The PMS calls this function to retrieve a band of data from the OIL.  The PMS
 * must specify the job, page, colorant and band number to identify the required
 * band.  It must also pass in an initialized pointer to a PMS_TyBand structure 
 * to receive the data from the OIL.
 * \param[in]        uJobID     The job ID which partially identifies the required band.
 * \param[in]        uPageID    The page ID which partially identifies the required band.
 * \param[in]        nColorant  The colorant identifier which partially identifies the required band.
 * \param[in]        nBandNo    The band number of the required band.
 * \param[in, out]   ptBand     An initialized pointer to a PMS band structure. The retrieved data will
 *                              be written to this structure.
 * \return           Returns TRUE if the requested band is successfully retrieved, FALSE otherwise.
 */
int OIL_GetBandData(unsigned int uJobID, unsigned int uPageID, int nColorant, int nBandNo, PMS_TyBand *ptBand)
{
  OIL_TyPage    *ptCurrentPage;
  OIL_TyJob    *ptJob;
  int retVal = FALSE;

  OIL_ProbeLog(SW_TRACE_OIL_GETBANDDATA, SW_TRACETYPE_ENTER, (intptr_t)nBandNo);

  if(ptBand == NULL)
  {
    HQFAIL("OIL_GetBandData: Invalid Band Pointer");
    return retVal;
  }

  ptJob = GetJobByJobID(uJobID);
  if(ptJob == NULL)
  {
    HQFAIL("OIL_GetBandData: Job for which band data is being requested is not present in job queue");
    return retVal;
  }

  /* start from the head of the page list */
  ptCurrentPage = ptJob->pPage;

  /* iterate over pages to find the page for which bands are requested */
  while(ptCurrentPage !=NULL)
  {
    if(ptCurrentPage->uPageNo == uPageID)
    {
      /* page found. fill the structure with band data */
      ptBand->uBandHeight = ptCurrentPage->atPlane[nColorant].atBand[nBandNo].uBandHeight;
      ptBand->uBandNumber = ptCurrentPage->atPlane[nColorant].atBand[nBandNo].uBandNumber;
      ptBand->cbBandSize = ptCurrentPage->atPlane[nColorant].atBand[nBandNo].cbBandSize;
      ptBand->pBandRaster = ptCurrentPage->atPlane[nColorant].atBand[nBandNo].pBandRaster;
      retVal = TRUE;
      break;
    }
    ptCurrentPage = (OIL_TyPage *)ptCurrentPage->pNext;
  }

  HQASSERT(ptCurrentPage != NULL,
           "OIL_GetBandData: Page for which band data is being requested is not present");

  OIL_ProbeLog(SW_TRACE_OIL_GETBANDDATA, SW_TRACETYPE_EXIT, (intptr_t)nBandNo);

  return retVal;
}

/**
 * \brief Embedded Device PostScript language backchannel support.
 *
 * This function is called by the embedded device to open a backchannel to the PMS.
 * In this implementation, there is only one backchannel provided, so this
 * function returns a hard-coded file descriptor.
 */
DEVICE_FILEDESCRIPTOR OIL_BackChannelOpen(void)
{
  /* only one backchannel so hardcode the file descriptor */
  return (1);
}

/**
 * \brief return  a detected error message pointer.
 *
 * This function is called form the OIL_start routine after  completing a job .
 * It returns a NULL pointer if no error has been detected (or Print Error Page is not selected)
 * \param[in]   val     if set to zero then resets the error code to 0.
 * \return      Returns the ponter to the message or NULL.
 */

/**
 * \brief Write to the PMS backchannel.
 *
 * This function is called by the embedded device to write to the PMS backchannel.
 * It writes the specified number of characters from the provided buffer to the 
 * backchannel.
 * \param[in]   buff    A character buffer containing the data to write to the backchannel.
 * \param[in]   len     The amount of data, in bytes, to be written to the backchannel.
 * \return      Returns the number of bytes successfully written to the backchannel.
 */
int OIL_BackChannelWrite(unsigned char * buff, int len)
{
  int retval;
  char szError[] = "%%[ Error:";

  OIL_ProbeLog(SW_TRACE_OIL_BACKCHANNELWRITE, SW_TRACETYPE_ENTER, (intptr_t)0);
  retval = PMS_PutBackChannel(buff, len);
  OIL_ProbeLog(SW_TRACE_OIL_BACKCHANNELWRITE, SW_TRACETYPE_EXIT, (intptr_t)0);
  /* Check for an actual PS error message */
  if(g_pstCurrentJob->uPrintErrorPage > 0)
  {
    if(strncmp((char *)buff, szError, strlen(szError)) == 0)
    {
      g_JobErrorData.Code = 1;
      if(len > OIL_MAX_ERRORMESSAGE_LENGTH)
        len = OIL_MAX_ERRORMESSAGE_LENGTH;
      strncpy(g_JobErrorData.szData, (char *)buff, len);
      g_JobErrorData.szData[len] = '\0';
    }
  }
  return (retval);
}

/**
 * \brief Open the specified file.
 *
 * This function is an interface to open a file on disk.
 * It sets the handle which can later be used to read, seek etc.
 * \param[in]   pzPath    character string specifying the file to open.
 * \param[in]   flags     character string specifying the file mode to be used.
 * \param[out]  pHandle   the passed address will point to the file handle on success.
 * \return      Returns TRUE if file was opened successfully, FALSE otherwise.
 */
int OIL_FileOpen(char * pzPath, char * flags, void ** pHandle)
{
  if(PMS_FileOpen( pzPath, flags, pHandle ) == TRUE)
  {
    return 1;
  }
  else
  {
    return -1;
  }
}

/**
 * \brief Closes the file associated with the specified handle.
 *
 * This function is an interface to close a file on disk.
 * \param[in]   pHandle  address pointing to the file handle.
 * \return      Returns TRUE on success, FALSE otherwise.
 */
int OIL_FileClose(void * handle)
{
  return (PMS_FileClose( handle ));
}

/**
 * \brief Read the file associated with the specified handle.
 *
 * This function is an interface to read a file on disk.
 * \param[out]  buffer   the requested bytes are read into the buffer pointed to by this address
 * \param[in]   nBytesToRead    number of bytes to read
 * \param[in]   pHandle   address pointing to the file handle.
 * \return      Returns number of bytes read on success, negative value otherwise.
 */
int OIL_FileRead(unsigned char * buffer, int nBytesToRead, void * handle)
{
  return (PMS_FileRead(buffer, nBytesToRead, handle));
}

/**
 * \brief Seek the file associated with the specified handle.
 *
 * This function is an interface to seek a file on disk.
 * \param[in]   pHandle   address pointing to the file handle.
 * \param[in]   nBytesToSeek   number of bytes to seek
 * \param[in]   nWhence    SEEK_CUR - seek from current location, 
 *                         SEEK_SET - seek from start of file, 
 *                         SEEK_END - seek relative to end of file, 
 * \return      Returns negative integer on failure.
 */
int OIL_FileSeek(void * handle, long *pPosition, int nWhence)
{
  return (PMS_FileSeek(handle, pPosition, nWhence));
}

/**
 * \brief Return file size in bytes of the file associated with the specified handle.
 *
 * This function is an interface for calculating file size of a file on disk.
 * \param[in]   pHandle   address pointing to the file handle.
 * \return      Returns file size in bytes
 */
int OIL_FileBytes(void * handle)
{
  return (PMS_FileBytes(handle));
}

/**
 * \brief Submit a band to PMS
 *
 * This routine passes the completed band to PMS. \n
 * It is called only in push band delivery model. \n
 */
int SubmitBandToPMS(PMS_TyBandPacket *ptBandpacket)
{
  static unsigned int uPageChecksum = 0;
  static unsigned int uBandSizeChecksum = 0;

  if(g_bPageChecksum) {

    /* If band packet is null, then it indicates end of page. */
    if(ptBandpacket==NULL) {

      /* Output to console (if enabled - why wouldn't the messages be enabled if checksums have been turn on?) */
      GG_SHOW(GG_SHOW_CHECKSUM, "Raster checksum: %d, 0x%08X\n", g_pstCurrentJob->uPagesToPrint, uPageChecksum);
      GG_SHOW(GG_SHOW_CHECKSUM, "Band size checksum: %d, 0x%08X\n", g_pstCurrentJob->uPagesToPrint, uBandSizeChecksum);

      /* Record checksum value */
      OIL_ProbeLog(SW_TRACE_OIL_PAGECHECKSUM, SW_TRACETYPE_MARK, (intptr_t)uPageChecksum);

      /* Clear for next page */
      uPageChecksum = 0;
      uBandSizeChecksum = 0;

    } else {
      unsigned char *p;
      unsigned int i;

      /* Record start of checksum calculation */
      OIL_ProbeLog(SW_TRACE_OIL_PAGECHECKSUM, SW_TRACETYPE_ENTER, (intptr_t)uPageChecksum);

      for(i=0; i < PMS_MAX_PLANES_COUNT; i++)
      {
        if(ptBandpacket->atColoredBand[i].ePlaneColorant != PMS_INVALID_COLOURANT)
        {
          if ( ptBandpacket->atColoredBand[i].pBandRaster) {
            for(p = ptBandpacket->atColoredBand[i].pBandRaster;
                p < ((ptBandpacket->atColoredBand[i].pBandRaster +
                      ptBandpacket->atColoredBand[i].cbBandSize));
                p++) {
              uPageChecksum += *p;
            } /* for each unsigned int */
            uBandSizeChecksum += ptBandpacket->atColoredBand[i].cbBandSize;
          } /* if raster data pointer valid (why wouldn't it be?) */
        } /* if plane contains valid data */
      } /* for each plane */

      /* Record end of checksum calculation */
      OIL_ProbeLog(SW_TRACE_OIL_PAGECHECKSUM, SW_TRACETYPE_EXIT, (intptr_t)uPageChecksum);

    } /* not end of job condition */
  } /* do checksums */

  /* checkin the band packet */
  return (PMS_CheckinBand(ptBandpacket));
}

/**
 * \brief Submit a fully rendered page to the PMS.
 *
 * This function is called once the OIL page is completely rendered.  Before submitting 
 * the page to the PMS, various conditions are checked.  The page will not be submitted 
 * if it is outside the range of pages to be printed for the job, or if it a blank page 
 * and blank page suppression has been activated.  A page which is does not pass these
 * tests is deleted.
 *
 * If fewer planes have been created for the page than expected for the colorant family, blank
 * planes will be generated as required, or the job forced to monochrome in the case of a 
 * CMYK job with blank color planes, if this functionality has been activated.
 *
 * Finally, if the page is to be submitted to the PMS, the completed OIL page structure 
 * is used to create a PMS page, which is passed to PMS_CheckinPage().  If the page is 
 * not to be submitted to the PMS, then the page is deleted immediately as the PMS
 * will not call the OIL function to indicate that the page has been output.
 */
void SubmitPageToPMS(void)
{
  OIL_TyPage **ptLastPage;
  PMS_TyPage *ptPMSPage;
  int bSubmitPage = TRUE;
#ifdef DIRECTPRINTPCLOUT
  PMS_TySystem ptPMSSysInfo;
#endif
  g_pstCurrentJob->uPagesParsed++;
#ifdef DIRECTPRINTPCLOUT
  PMS_GetSystemInfo(&ptPMSSysInfo , PMS_CurrentSettings);
  if((ptPMSSysInfo.eOutputType == PMS_DIRECT_PRINT) || (ptPMSSysInfo.eOutputType == PMS_DIRECT_PRINTXL))
  {
    GGglobal_timing(SW_TRACE_OIL_CHECKIN, 0);
    GGglobal_timing(SW_TRACE_OIL_PAGEDONE, 0);
    return;
  }
#endif
  OIL_ProbeLog(SW_TRACE_OIL_SUBMITPAGETOPMS, SW_TRACETYPE_ENTER, (intptr_t)g_pstCurrentJob->uPagesParsed);

#ifdef USE_PJL
  bSubmitPage = OIL_PjlShouldOutputPage(g_pstCurrentJob->uPagesParsed, g_pstCurrentJob);
#endif

  if (bSubmitPage)
  {
    if ((!g_pstCurrentPage) || (g_pstCurrentPage->nBlankPage == TRUE))
    {
      if (g_pstCurrentJob->bSuppressBlank == TRUE)
      {
        /* if blank pages are to be suppressed, then do not create the blank page. */
        bSubmitPage = FALSE;
      }
      else
      {
        /* we have no CurrentPage - because this is a blank page */
        GG_SHOW(GG_SHOW_OIL, "Blank page detected: create a blank page. \n ");
        g_pstCurrentPage = CreateOILBlankPage(g_pstCurrentPage, NULL);
        if(!g_pstCurrentPage)
        {
          /* There was some problem creating the blank page. Already would have been asserted */
          return;
        }
      }
    }
  }

  if (bSubmitPage)
  {
    g_pstCurrentPage->pstJob = g_pstCurrentJob;

    /* attach the page at the tail of the page list of current job */
    ptLastPage = &g_pstCurrentJob->pPage;
    while(*ptLastPage != NULL)
    {
        ptLastPage = (OIL_TyPage **)&(*ptLastPage)->pNext;
    }
    *ptLastPage = g_pstCurrentPage;
    g_pstCurrentJob->uPagesToPrint++;
    g_pstCurrentJob->uPagesInOIL++;


    /* create PMS page from the oil page */
    ptPMSPage = CreatePMSPage(g_pstCurrentPage);
    g_pstCurrentPage->ptPMSpage = ptPMSPage ;
    GGglobal_timing(SW_TRACE_OIL_CHECKIN, g_pstCurrentJob->uPagesToPrint);

    /* There is no data attached to the page structure when using the push band delivery
       methods. Instead the checksum is computed in the SubmitBandToPMS function above. */
    if(g_bPageChecksum && 
      (g_ConfigurableFeatures.eBandDeliveryType != OIL_PUSH_BAND) &&
      (g_ConfigurableFeatures.eBandDeliveryType != OIL_PUSH_BAND_DIRECT_SINGLE) &&
      (g_ConfigurableFeatures.eBandDeliveryType != OIL_PUSH_BAND_DIRECT_FRAME)
      )
    {
      uint8 *px;
      uint32 uBand;
      uint32 uColorant;
      uint32 uPageChecksum = 0;
      uint32 uBandSizeChecksum = 0;

      /* Record start of checksum calculation */
      OIL_ProbeLog(SW_TRACE_OIL_PAGECHECKSUM, SW_TRACETYPE_ENTER, (intptr_t)uPageChecksum);

      for(uColorant = 0; uColorant < OIL_MAX_PLANES_COUNT; uColorant++)
      {
        if(ptPMSPage->atPlane[uColorant].ePlaneColorant != PMS_INVALID_COLOURANT)
        {
          for(uBand = 0; uBand < ptPMSPage->atPlane[uColorant].uBandTotal; uBand++)
          {
            for(px = (uint8 *)ptPMSPage->atPlane[uColorant].atBand[uBand].pBandRaster; 
                px < (uint8 *)(ptPMSPage->atPlane[uColorant].atBand[uBand].pBandRaster + ptPMSPage->atPlane[uColorant].atBand[uBand].cbBandSize);
                px++)
            {
              uPageChecksum+=*px;
            }
            uBandSizeChecksum+=ptPMSPage->atPlane[uColorant].atBand[uBand].cbBandSize;
          }
        }
      }

      GG_SHOW(GG_SHOW_CHECKSUM, "Raster checksum: %d, 0x%08X\n", g_pstCurrentJob->uPagesToPrint, uPageChecksum);
      GG_SHOW(GG_SHOW_CHECKSUM, "Band size checksum: %d, 0x%08X\n", g_pstCurrentJob->uPagesToPrint, uBandSizeChecksum);

      /* Record end of checksum calculation */
      OIL_ProbeLog(SW_TRACE_OIL_PAGECHECKSUM, SW_TRACETYPE_EXIT, (intptr_t)uPageChecksum);
      /* Record checksum value */
      OIL_ProbeLog(SW_TRACE_OIL_PAGECHECKSUM, SW_TRACETYPE_MARK, (intptr_t)uPageChecksum);
    }

    /* checkin the page */
    PMS_CheckinPage(ptPMSPage);

    /* ready to start the page */
    GGglobal_timing(SW_TRACE_OIL_PAGESTART, 0);
  }
  else if (g_pstCurrentPage)
  {
    DeleteOILPage(g_pstCurrentPage);
    g_pstCurrentPage = NULL;
  }

  OIL_ProbeLog(SW_TRACE_OIL_SUBMITPAGETOPMS, SW_TRACETYPE_EXIT, (intptr_t)g_pstCurrentJob->uPagesParsed);
}

/**
 * \brief Get the media source and media size from the PMS.
 *
 * This routine copies the media attributes being requested by the job into a PMS_TyMedia 
 * structure and passes it to the PMS.  This gives the PMS the opportunity to 
 * compare the requested media attributes with those that are actually available, and
 * to update the job attributes accordingly.
 * \param[in, out]  stEBDDeviceParams   A pointer to an OIL_TyEBDDeviceParameters structure holding the
 *                                      media details from the current job. It will be updated if the PMS
 *                                      changes the media size or input tray.
 */
void GetMediaFromPMS(OIL_TyEBDDeviceParameters *stEBDDeviceParams)
{
  PMS_TyMedia *pstPMSMedia;
  PMS_TyTrayInfo *ptTrayInfo;
  PMS_TyPaperInfo *pPaperInfo;
  PMS_TyJob *pCurrentJob;
  int rotate;
  float ClipLeft, ClipTop, ClipWidth, ClipHeight;

  OIL_ProbeLog(SW_TRACE_OIL_GETMEDIAFROMPMS, SW_TRACETYPE_ENTER, (intptr_t)0);
  pstPMSMedia = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, sizeof(PMS_TyMedia));
  HQASSERTV(pstPMSMedia!=NULL,
            ("GetMediaFromPMS: Failed to allocate %lu bytes", (unsigned long) sizeof(PMS_TyMedia)));

  /* Clear the structure to avoid confusion when debugging */
  memset(pstPMSMedia, 0x00, sizeof(PMS_TyMedia));

  /* The parameters in stEBDDeviceParams configured in the (%embedded%) device. 
     see \HqnEbd_SensePageDevice in SW\procsets\HqnEmbedded.
     see ebd_devparams_array in src\oil_ebddev.c  */
  strcpy((char*)pstPMSMedia->szMediaType, (char*)stEBDDeviceParams->MediaTypeFromJob);
  strcpy((char*)pstPMSMedia->szMediaColor, (char*)stEBDDeviceParams->MediaColorFromJob);
  pstPMSMedia->uMediaWeight = stEBDDeviceParams->MediaWeightFromJob;
  pstPMSMedia->uInputTray = stEBDDeviceParams->MediaSourceFromJob;
  pstPMSMedia->dWidth = stEBDDeviceParams->PageWidthFromJob;
  pstPMSMedia->dHeight = stEBDDeviceParams->PageHeightFromJob;

  /* Access default job settings */
  PMS_GetJobSettings(&pCurrentJob);
  HQASSERT(pCurrentJob, "GetMediaFromPMS, pCurrentJob is NULL");
  pstPMSMedia->ePaperSize = pCurrentJob->tDefaultJobMedia.ePaperSize;

  /* \todo Populate pstPMSMedia with other parameters that influence tray selection */

  /* call PMS function to allow PMS to select a tray/media. */
  if(PMS_MediaSelect(pstPMSMedia, &ptTrayInfo, &rotate)) {
    HQASSERT(ptTrayInfo,
             "GetMediaFromPMS, tray/media selected, but ptTrayInfo is NULL");

    if(PMS_GetPaperInfo(ptTrayInfo->ePaperSize, &pPaperInfo)) {
      HQASSERT(pPaperInfo, "GetMediaFromPMS, pPaperInfo is NULL");

      stEBDDeviceParams->PageWidthFromPMS = (float)pPaperInfo->dWidth;
      stEBDDeviceParams->PageHeightFromPMS = (float)pPaperInfo->dHeight;
      stEBDDeviceParams->PCL5PageSizeFromPMS = pPaperInfo->ePCL5size;
      stEBDDeviceParams->PCLXLPageSizeFromPMS = pPaperInfo->ePCLXLsize;

      ClipLeft = (float)(pPaperInfo->nLeftUnprintable * 0.000072);
      ClipTop = (float)(pPaperInfo->nTopUnprintable * 0.000072);
      ClipWidth = (float)(pPaperInfo->dWidth - ClipLeft - (pPaperInfo->nRightUnprintable * 0.000072));
      ClipHeight = (float)(pPaperInfo->dHeight - ClipTop - (pPaperInfo->nBottomUnprintable * 0.000072));

      if(rotate)
      {
        stEBDDeviceParams->MediaClipTopFromPMS = ClipLeft;
        stEBDDeviceParams->MediaClipLeftFromPMS = ClipTop;
        stEBDDeviceParams->MediaClipHeightFromPMS = ClipWidth;
        stEBDDeviceParams->MediaClipWidthFromPMS = ClipHeight;
      }
      else
      {
        stEBDDeviceParams->MediaClipLeftFromPMS = ClipLeft;
        stEBDDeviceParams->MediaClipTopFromPMS = ClipTop;
        stEBDDeviceParams->MediaClipWidthFromPMS = ClipWidth;
        stEBDDeviceParams->MediaClipHeightFromPMS = ClipHeight;
      }

      if(g_ConfigurableFeatures.ePrintableMode == OIL_RIPRemovesUnprintableArea)
      {
        /* calculate the logical page or get engine raster dimensions if defined */

        stEBDDeviceParams->ImagingBoxLeftFromPMS = ClipLeft;
        stEBDDeviceParams->ImagingBoxTopFromPMS = ClipTop;
        stEBDDeviceParams->ImagingBoxRightFromPMS = ClipLeft +
          PADDED_ALIGNEMENT(ClipWidth, g_pstCurrentJob->uRIPDepth, g_pstCurrentJob->uXResolution);
        stEBDDeviceParams->ImagingBoxBottomFromPMS = ClipTop + ClipHeight;
      }
      else
      {
        stEBDDeviceParams->ImagingBoxLeftFromPMS = 0;
        stEBDDeviceParams->ImagingBoxTopFromPMS = 0;
        stEBDDeviceParams->ImagingBoxRightFromPMS = 
          PADDED_ALIGNEMENT(pPaperInfo->dWidth, g_pstCurrentJob->uRIPDepth, g_pstCurrentJob->uXResolution);
        stEBDDeviceParams->ImagingBoxBottomFromPMS = (float)pPaperInfo->dHeight;
      }

      stEBDDeviceParams->MediaSourceFromPMS = ptTrayInfo->eMediaSource;

    } else {
      HQFAIL("GetMediaFromPMS, Call to PMS_GetPaperInfo failed.");
    }
  }

  OIL_free(OILMemoryPoolJob, pstPMSMedia);
  OIL_ProbeLog(SW_TRACE_OIL_GETMEDIAFROMPMS, SW_TRACETYPE_EXIT, (intptr_t)0);
}

/**
 * \brief Get the band size specification from the PMS.
 *
 * This routine passes the page width, height and raster depth to the PMS, along with a 
 * pointer to a PMS_TyBandInfo structure.  This gives the PMS the opportunity to 
 * specify an appropriate band size based on the memory requirements of the page.
 * \param[in]       nPageWidth      The width of the page, in pixels.
 * \param[in]       nPageHeight     The height of the page, in pixels.
 * \param[in]       uRasterDepth    The bit depth of the rastered image.
 * \param[in, out]  stPMSBandInfo   A pointer to an PMS_TyBandInfo structure to receive the band 
 *                                  size specified by the PMS.  This should be a valid, pointer 
 *                                  to already allocated memory.
 */
void GetBandInfoFromPMS(int nPageWidth, int nPageHeight, unsigned int uRasterDepth, 
                        PMS_TyBandInfo * stPMSBandInfo)
{
  OIL_ProbeLog(SW_TRACE_OIL_GETBANDINFOFROMPMS, SW_TRACETYPE_ENTER, (intptr_t)0);
  /* call PMS callback function to allow PMS to decide the band size*/
  PMS_GetBandInfo(nPageWidth, nPageHeight, uRasterDepth, stPMSBandInfo);
  OIL_ProbeLog(SW_TRACE_OIL_GETBANDINFOFROMPMS, SW_TRACETYPE_EXIT, (intptr_t)0);
}
/**
 * \brief Retrieve screen table data from the PMS.
 *
 * This routine receives an integer specifying a particular screen and retrieves 
 * a pointer to the corresponding array of PMS_TyScreenInfo structures to the PMS.
 * \param[in]     nScreen            An integer specifying which screen's information is required.
 * \param[out]    ppPMSScreenInfo    A pointer to a pointer of PMS screen information structures.
 *                                   This will be updated to point to the required screen information.
 * \return   Returns TRUE if the screen information is successfully retrieved, FALSE otherwise.
 */
int GetScreenInfoFromPMS(int nScreen, PMS_TyScreenInfo **ppPMSScreenInfo)
{
  OIL_ProbeLog(SW_TRACE_OIL_GETSCREENINFOFROMPMS, SW_TRACETYPE_ENTER, (intptr_t)0);
    if(!PMS_GetScreenInfo(nScreen, ppPMSScreenInfo))
    {
        GG_SHOW(GG_SHOW_OIL, "GetScreenTableFromPMS: Failed to get screen info, %d.\n", nScreen);
        return FALSE;
    }
  OIL_ProbeLog(SW_TRACE_OIL_GETSCREENINFOFROMPMS, SW_TRACETYPE_EXIT, (intptr_t)0);

    return TRUE;
}

/**
 * \brief Populates the OIL state variables with PMS system data.
 *
 * This function retrieves the PMS system information and copies essential data
 * from it into the OIL's global variables.  This includes:
 * \arg  The size of the memory allocated for RIP usage;
 * \arg  The custom configuration data;
 * \arg  The active paper selection mode;
 * \arg  The width of traps to be applied;
 * \arg  The active color management method, and
 * \arg  The active probe trace options.
 */
void GetPMSSystemInfo()
{
    PMS_TySystem ptPMSSysInfo;
    OIL_ProbeLog(SW_TRACE_OIL_GETPMSSYSTEMINFO, SW_TRACETYPE_ENTER, (intptr_t)0);

    PMS_GetSystemInfo(&ptPMSSysInfo , PMS_CurrentSettings);
    g_SystemState.cbRIPMemory = ptPMSSysInfo.cbRIPMemory;
    g_SystemState.nOILconfig = ptPMSSysInfo.nOILconfig;
    g_SystemState.nOutputType = ptPMSSysInfo.eOutputType;
    switch(ptPMSSysInfo.ePaperSelectMode)
    {
    case PMS_PaperSelNone:
      g_ConfigurableFeatures.g_ePaperSelectMode = OIL_PaperSelNone;
      break;
    case PMS_PaperSelRIP:
      g_ConfigurableFeatures.g_ePaperSelectMode = OIL_PaperSelRIP;
      break;
    case PMS_PaperSelPMS:
      g_ConfigurableFeatures.g_ePaperSelectMode = OIL_PaperSelPMS;
      break;
    default:
      HQFAILV(("GetPMSSystemInfo: Unsupported paper select mode (%d), setting default None", ptPMSSysInfo.ePaperSelectMode));
      g_ConfigurableFeatures.g_ePaperSelectMode = OIL_PaperSelNone;
      break;
    }
    g_ConfigurableFeatures.ePrintableMode = ptPMSSysInfo.nPrintableMode;
    g_ConfigurableFeatures.nStrideBoundaryBytes = ptPMSSysInfo.nStrideBoundaryBytes;

    g_ConfigurableFeatures.fEbdTrapping = ptPMSSysInfo.fTrapWidth;
    g_ConfigurableFeatures.uColorManagement = ptPMSSysInfo.uColorManagement;
    strcpy(g_ConfigurableFeatures.szProbeTrace, ptPMSSysInfo.szProbeTraceOption);
    g_ConfigurableFeatures.nRendererThreads = ptPMSSysInfo.nRendererThreads;

#ifdef  DIRECTPRINTPCLOUT
    if((ptPMSSysInfo.eOutputType == PMS_DIRECT_PRINT) || (ptPMSSysInfo.eOutputType == PMS_DIRECT_PRINTXL))
    {
      g_ConfigurableFeatures.bPixelInterleaving = TRUE;
      /* now protect the direct print mode as it does not handle the direct raster band delivery mode*/
      ptPMSSysInfo.eBandDeliveryType = PMS_PUSH_PAGE;
      /* copy local setting to PMS job setting */
      PMS_SetSystemInfo(&ptPMSSysInfo , PMS_CurrentSettings);
    }
#endif
    g_ConfigurableFeatures.eBandDeliveryType = ptPMSSysInfo.eBandDeliveryType;

    OIL_ProbeLog(SW_TRACE_OIL_GETPMSSYSTEMINFO, SW_TRACETYPE_EXIT, (intptr_t)0);
}

