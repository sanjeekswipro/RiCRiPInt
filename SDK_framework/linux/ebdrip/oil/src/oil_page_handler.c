/* Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_page_handler.c(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief This file contains the implementation of the OIL's page handling functionality.
 *
 * This file contains routines to manage oil's page structures
 *
 */
#include <string.h>
#include "oil.h"
#include "std.h"
#include "skinras.h"
#include "pms_export.h"
#include "oil_page_handler.h"
#include "oil_malloc.h"
#include "skinkit.h"
#include "oil_job_handler.h"
#include "oil_interface_oil2pms.h"
#ifdef USE_PJL
#include "oil_pjl.h"
#endif
#ifdef USE_UFST5
#include "pfinufst5.h"
#define PCLFONTLIST
#endif
#ifdef USE_UFST7
#include "pfinufst7.h"
#define PCLFONTLIST
#endif
#ifdef USE_FF
#include "pfinff.h"
#define PCLFONTLIST
#endif
/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;
extern OIL_TyError g_JobErrorData;
extern OIL_TyPage *g_pstCurrentPage;

static char SetA4[] = "<< /PageSize [ 595 842 ] /ImagingBBox null >> setpagedevice ";
static char SetLetter[] = "<< /PageSize [ 612 792 ] /ImagingBBox null >> setpagedevice ";

static char fontlistHeader[] =
  "%!PS-Adobe-3.0  \n\
%%Creator: Global Graphics Software 2010  \n\
%% List and Proof Available Fonts. \n\n";

static char fontlistUtilities[] =
  "%s \n \
    << /NumCopies %d >> setpagedevice \n \
    currentpagedevice /PageSize get aload pop \n \
    /y exch def \n \
    /x exch def \n \
    /height y 122 sub def \n \
    /pagenumber 1 def \n \
    /newpage { 72 y 62 sub moveto header /pagenumber pagenumber 1 add def   \n \
               /height y 122 sub def } bind def  \n \
    /header { \n \
     gsave currentpoint translate  \n \
     /Helvetica-Bold findfont 24 scalefont setfont \n \
     (%s %s) dup stringwidth pop 2 div 220 exch sub \n \
     currentpoint exch pop moveto show \n \
     0.3 setgray 0 -45 440 30 rectfill \n \
     /Helvetica-Bold  findfont 20 scalefont setfont 1.0 setgray  \n \
     5 -38 moveto (PS Font List) show \n \
     400 -38 moveto pagenumber 5 string cvs show \n \
     grestore \n \
   } def \n \
    /bubble {  \n \
      /Array exch def  \n \
      0 1 Array length 2 sub {  \n \
        -1 0 {  \n \
          /i exch def  \n \
          Array i 2 getinterval  \n \
          aload pop  \n \
          2 copy  \n \
          gt {  \n \
            exch 2 array astore  \n \
            Array exch i exch  \n \
            putinterval  \n \
          } { pop pop } ifelse  \n \
        } for  \n \
      } for  \n \
      Array  \n \
    } def  \n \
    /showfontline { \n \
      12 scalefont setfont  \n \
      height 92 lt { showpage newpage } if  \n \
      /height height 20 sub def  \n \
      300 height moveto   \n \
      show \n \
      72 height moveto  \n \
      /Helvetica findfont 12 scalefont setfont  \n \
      show  \n \
    } def \n\n";

static char fontlistProof[] =
    "    newpage \n \
    0.0 setgray   \n \
    % Fonts excluding CIDfonts (uses a list of fonts in a dictionary)  \n \
    /cnt 0 def  \n \
    (*)  \n \
    {  \n \
        dup findfont dup /FontType get 0 eq  \n \
        {  \n \
            dup /FMapType get 9 ne  \n \
            {  \n \
                pop dup length string copy   \n \
                /cnt cnt 1 add def  \n \
            }  \n \
            {  \n \
                begin  \n \
                    /CMap where  \n \
                    {  \n \
                        pop pop  \n \
                    }  \n \
                    {  \n \
                        dup length string copy   \n \
                        /cnt cnt 1 add def  \n \
                    } ifelse  \n \
                end  \n \
            }ifelse  \n \
        }  \n \
        {  \n \
            pop dup length string copy   \n \
            /cnt cnt 1 add def  \n \
        } ifelse  \n \
    } 256 string /Font resourceforall  \n \
    \n \
    cnt 0 ne {  \n \
        /FontList cnt array def  \n \
        FontList astore  \n \
        \n \
        FontList bubble   \n \
        {  \n \
            dup (ABCDEfghij-12345) exch findfont    \n \
            showfontline  \n \
        } forall  \n \
        \n \
        pop  \n \
    } if  \n \
    \n \
    \n \
    % CIDFonts (uses a list of fonts in an array)  \n \
    /cnt 0 def  \n \
    (*)   \n \
    {   \n \
        dup length string copy   \n \
        /cnt cnt 1 add def   \n \
    } 256 string /CIDFont resourceforall  \n \
    \n \
    cnt 0 ne {  \n \
        /CidFontList cnt array def  \n \
        CidFontList astore  \n \
         \n \
        CidFontList bubble   \n \
        {  \n \
            dup cvn 1 array astore /currentfont /Katakana 3 -1 roll composefont \n \
            (ABCDEfghij-12345) exch \n \
            showfontline  \n \
        } forall  \n \
        \n \
        pop  \n \
    } if  \n \
    \n \
    showpage \n\n";

static char pszErrorPage[] =
  "%%!PS-Adobe-3.0  \n \
  %s \n \
  /Helvetica findfont 12 scalefont setfont  \n \
  0 setgray \n \
  36 144 moveto (%s) show \n \
  showpage \n\n";

/**
 * \brief Create and populate an OIL page structure.
 *
 * This function accepts a raster description structure as a parameter.  
 * It creates an OIL_TyPage structure and uses the raster data to populate
 * the attributes of the new page structure, including raster resolution, bit
 * depth, image dimensions, colorant family, media dimensions and print options
 * such as collation and duplexing.  The planes of the page are initialized
 * with the colorants appropriate to the active colorant family, marked as 
 * empty planes and their band data set to null.
 *
 * \param[in]  ptRasterDescription  A raster description structure containing the page information.
 * \return     Returns a pointer to the newly created page structure.
 */
OIL_TyPage* CreateOILPage(struct rasterDescription * ptRasterDescription)
{
  OIL_TyPage *ptPage;
  int i, j;
  /*TODO: need to take care of memory allocation fail */
  ptPage = (OIL_TyPage *)OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, sizeof(OIL_TyPage));
  HQASSERTV(ptPage!=NULL,
            ("CreateOILPage: Failed to allocate %lu bytes", (unsigned long) sizeof(OIL_TyPage)));

  ptPage->pNext = NULL;
  ptPage->uPageNo = g_pstCurrentJob->uPagesToPrint+1;
  ptPage->nPageWidthPixels = ptRasterDescription->imageWidth;
  ptPage->nPageHeightPixels = ptRasterDescription->imageHeight;
  ptPage->nRasterWidthData = ptRasterDescription->dataWidth;
  ptPage->dXRes = ptRasterDescription->xResolution;
  ptPage->dYRes = ptRasterDescription->yResolution;
  ptPage->uRIPDepth = ptRasterDescription->rasterDepth;
  ptPage->nColorants = ptRasterDescription->nSeparations;

  if(g_pstCurrentJob->bOutputDepthMatchesRIP) {
    ptPage->uOutputDepth = ptPage->uRIPDepth;
  }
  else {
    ptPage->uOutputDepth = g_pstCurrentJob->uOutputDepth;
  }

  for(i=0; i < OIL_MAX_PLANES_COUNT; i++)
  {
    ptPage->atPlane[i].uBandTotal = 0;
    ptPage->atPlane[i].bBlankPlane = TRUE;
    for (j=0; j< OIL_BAND_LIMIT; j++)
      ptPage->atPlane[i].atBand[j].pBandRaster = 0;
  }
  if(strcmp((char*)ptRasterDescription->colorantFamily, "DeviceCMYK")== 0)
  {
    ptPage->eColorantFamily = OIL_ColorantFamily_CMYK;
    ptPage->atPlane[0].ePlaneColorant = OIL_Cyan; /*Cyan*/
    ptPage->atPlane[1].ePlaneColorant = OIL_Magenta; /*Magenta*/
    ptPage->atPlane[2].ePlaneColorant = OIL_Yellow; /*Yellow*/
    ptPage->atPlane[3].ePlaneColorant = OIL_Black; /*Black*/
  }
  else if(strcmp((char*)ptRasterDescription->colorantFamily, "DeviceRGB")== 0)
  {
    ptPage->eColorantFamily = OIL_ColorantFamily_RGB;
    ptPage->atPlane[0].ePlaneColorant = OIL_Red; /*Red*/
    ptPage->atPlane[1].ePlaneColorant = OIL_Green; /*Green*/
    ptPage->atPlane[2].ePlaneColorant = OIL_Blue; /*Blue*/
    ptPage->atPlane[3].ePlaneColorant = OIL_InvalidColor; 
  }
  else if(strcmp((char*)ptRasterDescription->colorantFamily, "DeviceGray")== 0)
  {
    ptPage->eColorantFamily = OIL_ColorantFamily_Gray;
    ptPage->atPlane[0].ePlaneColorant = OIL_InvalidColor; 
    ptPage->atPlane[1].ePlaneColorant = OIL_InvalidColor; 
    ptPage->atPlane[2].ePlaneColorant = OIL_InvalidColor; 
    ptPage->atPlane[3].ePlaneColorant = OIL_Black; /*Black*/
  }
  else
  {
    ptPage->eColorantFamily = OIL_ColorantFamily_Unsupported;
  }

  ptPage->uCopies = ptRasterDescription->noCopies;
  ptPage->stMedia.uInputTray = ptRasterDescription->mediaSelect;
  ptPage->stMedia.uOutputTray = ptRasterDescription->outputAttributes;
  strcpy((char*)ptPage->stMedia.szMediaType, (char*)g_pstCurrentJob->tCurrentJobMedia.szMediaType);
  strcpy((char*)ptPage->stMedia.szMediaColor, (char*)g_pstCurrentJob->tCurrentJobMedia.szMediaColor); 
  ptPage->stMedia.uMediaWeight = g_pstCurrentJob->tCurrentJobMedia.uMediaWeight;
  ptPage->stMedia.dWidth = (ptRasterDescription->imageWidth / ptRasterDescription->xResolution) * 72;
  ptPage->stMedia.dHeight = (ptRasterDescription->imageHeight / ptRasterDescription->yResolution) * 72;
  ptPage->nTopMargin = ptRasterDescription->topMargin;
  ptPage->nBottomMargin = ptRasterDescription->bottomMargin;
  ptPage->nLeftMargin = ptRasterDescription->leftMargin;
  ptPage->nRightMargin = ptRasterDescription->rightMargin;
  ptPage->bIsRLE = ptRasterDescription->runLength;
  ptPage->bDuplex = ptRasterDescription->duplex;
  ptPage->bTumble = ptRasterDescription->tumble;
  ptPage->uOrientation = ptRasterDescription->orientation;
  ptPage->bCollate = ptRasterDescription->collate;
  ptPage->bFaceUp = ptRasterDescription->outputFaceUp;
  ptPage->nBlankPage = 1;
  ptPage->bPageComplete = FALSE;
  ptPage->pstJob = g_pstCurrentJob;
  ptPage->ptBandPacket = NULL;
  return ptPage;
}

/**
 * \brief Fill in the OIL band packet structure
 *
 * PMS band packet structure is created and populated with data from the
 * current page structure
 */
PMS_TyBandPacket *CreateBandPacket(int nColorants, int nColorFamilyOffset, int nSeparations, int nReqBytesPerLine, int nReqLinesPerBand, short Map[])
{
  int j;
  PMS_TyBandPacket *ptBandPacket;
  /* initialize the band buffers */
  /* allocate memory for the band */
  ptBandPacket= (PMS_TyBandPacket *)OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, sizeof(PMS_TyBandPacket));
  if(!ptBandPacket)
  {
    HQASSERTV(ptBandPacket!=NULL,
              ("CreateBandPacket: Failed to allocate %d bytes", (sizeof(PMS_TyBandPacket))));
    return NULL;
  }

  /* initialize the band buffers */
  ptBandPacket->uBandNumber = 0;
  ptBandPacket->uTotalPlanes = nColorants;
  for(j=0; j < OIL_MAX_PLANES_COUNT; j++)
  {
    ptBandPacket->atColoredBand[j].ePlaneColorant = PMS_INVALID_COLOURANT;
  }

  if(nSeparations > 1) /* in case of separations there will be only 1 color. Allocate 0th band */
  {
    /* determine the colorants of the planes */
    ptBandPacket->atColoredBand[0].ePlaneColorant = (PMS_eColourant)(Map[0] + nColorFamilyOffset);

    /* in case of separations, the band packet will have data only in first plane 
    irrespective of the colour. so set the mapping table to first plane */
    Map[0]=0;
  }
  else /* composite job */
  {
    for(j=0; j < nColorants; j++)
    {
      if(Map[j] != -1)
      {
        /* determine the colorants of the planes */
        ptBandPacket->atColoredBand[Map[j]].ePlaneColorant = (PMS_eColourant)(Map[j] + nColorFamilyOffset);
      }
    }
  }

  for(j=0; j < OIL_MAX_PLANES_COUNT; j++)
  {
    /* initialize the band planes */
    ptBandPacket->atColoredBand[j].uBandHeight = 0;
    ptBandPacket->atColoredBand[j].cbBandSize = 0;
    /* allocate memory for the band data only in valid planes, if
       we're not in band direct mode */
    if(g_ConfigurableFeatures.eBandDeliveryType != OIL_PUSH_BAND_DIRECT_SINGLE &&
       g_ConfigurableFeatures.eBandDeliveryType != OIL_PUSH_BAND_DIRECT_FRAME &&
       ptBandPacket->atColoredBand[j].ePlaneColorant != PMS_INVALID_COLOURANT)
    {
      ptBandPacket->atColoredBand[j].pBandRaster = (unsigned char *)OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, (nReqLinesPerBand * nReqBytesPerLine));
      if(!ptBandPacket->atColoredBand[j].pBandRaster)
      {
        HQASSERTV(ptBandPacket->atColoredBand[j].pBandRaster!=NULL,
                  ("CreateBandPacket: Failed to allocate %d bytes", (nReqLinesPerBand * nReqBytesPerLine)));
        return NULL;
      }
    }
    else
    {
      ptBandPacket->atColoredBand[j].pBandRaster = NULL;
    }
  }
  return ptBandPacket;
}

/**
 * \brief Processes notification of a 'page done' event.
 *
 * This function is called once a page has been fully processed by the PMS.  It
 * finds the specified page in the specified job and deletes it. It also updates 
 * the number of pages in the job and the number of printed pages in the OIL's job 
 * record.
 *
 * There can be no further reference to the page 
 * after this call completes. In addition, for the last page of a job, then job itself 
 * must not be referenced after the call to DeleteOILPage; the other thread will 
 * delete the job once all pages are deleted and the job finished.
 * \param[in] JobID     The ID of the job to which the page belongs.
 * \param[in] PageID    The ID of the page within the specified job.
 */
void ProcessPageDone(unsigned int JobID, unsigned int PageID)
{
  OIL_TyPage    *ptPageToDelete, **ptCurrentPage;
  OIL_TyJob    *ptJob;

  ptJob = GetJobByJobID(JobID);
  HQASSERT(ptJob != NULL, "ProcessPageDone: Job not found");
  HQASSERT(ptJob->pPage != NULL, "ProcessPageDone: OIL Page List is empty");

  /* start from the head of the page list */
  ptCurrentPage = &ptJob->pPage;

  /* iterate over pages to find page to delete */
  while(*ptCurrentPage !=NULL)
  {
    if((*ptCurrentPage)->uPageNo == PageID)
    {
#ifdef USE_PJL
      OIL_PjlReportPageStatus( PageID );
#endif

      ptPageToDelete = *ptCurrentPage;
      /* remove page from link list of pages */
      *ptCurrentPage = (OIL_TyPage *)(*ptCurrentPage)->pNext;
      ptJob->uPagesInOIL--;
      ptJob->uPagesPrinted++;
      /* free resources held by the page */
      DeleteOILPage(ptPageToDelete);

      /* Nothing more to do, so lets get out as soon as possible */
      return;
    }
    ptCurrentPage = (OIL_TyPage **)&(*ptCurrentPage)->pNext;
  }

  /* If we are here then the page was not found in the loop above */
  HQFAIL("ProcessPageDone: Received PAGEDONE for non-existent page");
}

/**
 * \brief Delete an OIL page structure
 *
 * This function frees all the plane data belonging to the supplied OIL 
 * page structure and finally frees the page structure itself.
 * There can be no reference to the page after this call completes.
 * \param[in] ptOILPage A pointer to the page structure which is to be deleted.
 */
void DeleteOILPage(OIL_TyPage *ptOILPage)
{
  int i;
  unsigned int j;
  if (g_ConfigurableFeatures.eBandDeliveryType != OIL_PUSH_BAND_DIRECT_SINGLE &&
      g_ConfigurableFeatures.eBandDeliveryType != OIL_PUSH_BAND_DIRECT_FRAME)
  {
    for(i=0; i < OIL_MAX_PLANES_COUNT; i++)
    {
      for(j=0; j < ptOILPage->atPlane[i].uBandTotal; j++)
      {
        if ( ptOILPage->atPlane[i].atBand[j].pBandRaster)
          OIL_free(OILMemoryPoolJob, ptOILPage->atPlane[i].atBand[j].pBandRaster);
      }
    }
  }

  /* in case of push band (on the fly) raster delivery model, we need to free the memory 
  allocated for the band buffer packets */
  if (ptOILPage->ptBandPacket)
  {
    if (g_ConfigurableFeatures.eBandDeliveryType == OIL_PUSH_BAND)
    {
      PMS_TyBandPacket *ptBandPacket;
      ptBandPacket = (PMS_TyBandPacket *)ptOILPage->ptBandPacket;
      for(j=0; j < OIL_MAX_PLANES_COUNT; j++)
      {
        if(ptBandPacket->atColoredBand[j].pBandRaster)
        {
          OIL_free(OILMemoryPoolJob,(void *)ptBandPacket->atColoredBand[j].pBandRaster);
        }
      }
      OIL_free(OILMemoryPoolJob,(void *)ptBandPacket);
    }
    else if (g_ConfigurableFeatures.eBandDeliveryType == OIL_PUSH_BAND_DIRECT_SINGLE ||
             g_ConfigurableFeatures.eBandDeliveryType == OIL_PUSH_BAND_DIRECT_FRAME)
    {
      OIL_free(OILMemoryPoolJob,(void *)ptOILPage->ptBandPacket);
    }
  }

  /* free the page structure */
  OIL_free(OILMemoryPoolJob, ptOILPage);
  if(g_pstCurrentJob != NULL)
  {
    g_pstCurrentJob->pPage = NULL;
    g_pstCurrentPage = NULL;
  }

}

/**
 * \brief Create a blank OIL page structure
 *
 * This function creates an OIL page structure which is populated with 
 * blank plane data.  If a non-null pointer is passed in to the function,
 * it is simply populated with the appropriate number of blank planes, as
 * dictated by the page's colorant family.  If a NULL pointer is passed in,
 * the function will allocate memory for a new page structure, and populate
 * it with data obtained from the RIP's \c pagebuffer device before creating
 * the blank planes.
 * \param[in, out]   ptOILPage   A pointer to a page structure.  It is allowable
 * for this pointer to be NULL, in which case memory will be allocated for a new 
 * structure.  If this pointer is not null, the structure will be populated with 
 * blank plane data.
 * \return  A pointer to the blank page structure.
 */
OIL_TyPage* CreateOILBlankPage(OIL_TyPage *ptOILPage, struct rasterDescription *pstRasterDescription)
{
  void *pgbdev = NULL ;
  int xResolution, yResolution, nColorantFamilyLen;
  float xResFrac, yResFrac;
  unsigned char *strColorantFamily;
  struct rasterDescription stRasterDescription;

  /* get handle to the page buffer device */
  pgbdev = SwLeGetDeviceHandle( (uint8*) "pagebuffer" );
  if (pgbdev == NULL)
  {
    HQFAIL("CreateOILBlankPage: SwLeGetDeviceHandle failed to get Pagebuffer device handle");
    return NULL;
  }

  /* if OilPage is not created before, create it */
  if(!ptOILPage)
  {
    /* get device parameters from the page buffer device and fill in the Raster Description structure */

    SwLeGetIntDevParam( pgbdev, (unsigned char *) "ImageWidth", &stRasterDescription.imageWidth);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "ImageHeight", &stRasterDescription.imageHeight);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "BandWidth", &stRasterDescription.dataWidth);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "XResolution", &xResolution);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "YResolution", &yResolution);
    SwLeGetFloatDevParam( pgbdev, (unsigned char *) "XResFrac", &xResFrac);
    SwLeGetFloatDevParam( pgbdev, (unsigned char *) "YResFrac", &yResFrac);
    stRasterDescription.xResolution = (double)xResolution + (double)xResFrac;
    stRasterDescription.yResolution = (double)yResolution + (double)yResFrac;
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "RasterDepth", (int*)&stRasterDescription.rasterDepth);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "NumCopies", (int*)&stRasterDescription.noCopies);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "InputAttributes", (int*)&stRasterDescription.mediaSelect);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "OutputAttributes", (int*)&stRasterDescription.outputAttributes);

    SwLeGetIntDevParam( pgbdev, (unsigned char *) "TopMargin", &stRasterDescription.topMargin);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "BottomMargin", &stRasterDescription.bottomMargin);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "LeftMargin", &stRasterDescription.leftMargin);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "RightMargin", &stRasterDescription.rightMargin);

    SwLeGetBoolDevParam( pgbdev, (unsigned char *) "RunLength", &stRasterDescription.runLength);
    SwLeGetBoolDevParam( pgbdev, (unsigned char *) "NegativePrint", &stRasterDescription.imageNegate);
    SwLeGetStringDevParam( pgbdev, (unsigned char *) "ColorantFamily", &strColorantFamily, &nColorantFamilyLen);
    strcpy((char*)stRasterDescription.colorantFamily, (char*)strColorantFamily);
    SwLeGetBoolDevParam( pgbdev, (unsigned char *) "Duplex", &stRasterDescription.duplex);
    SwLeGetBoolDevParam( pgbdev, (unsigned char *) "Tumble", &stRasterDescription.tumble);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "Orientation", &stRasterDescription.orientation);
    SwLeGetBoolDevParam( pgbdev, (unsigned char *) "Collate", &stRasterDescription.collate);
    SwLeGetBoolDevParam( pgbdev, (unsigned char *) "OutputFaceUp", &stRasterDescription.outputFaceUp);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "NumChannels", &stRasterDescription.numChannels);
    SwLeGetIntDevParam( pgbdev, (unsigned char *) "NumSeparations", &stRasterDescription.nSeparations);
    /* we need to set up for RGB as we cannot assume empty planes to get blank output */
    if(stRasterDescription.numChannels != 3)
      stRasterDescription.numChannels = 1;
    if(stRasterDescription.nSeparations != 3)
      stRasterDescription.nSeparations = 1;

    /* allocate memory for the page and fill the page structure*/
    ptOILPage = CreateOILPage(&stRasterDescription);
  }

  ptOILPage->nColorants = 0; 

  ptOILPage->nBlankPage = 1;
  ptOILPage->bPageComplete = TRUE;
  if(pstRasterDescription != NULL)
    *pstRasterDescription = stRasterDescription;
  return ptOILPage;
}

/**
 * \brief Create a blank OIL plane structure.
 *
 * This function uses the specified colorant and sample plane to create a plane of 
 * blank data, which is then inserted into the supplied page structure.  A blank 
 * plane is not the same as an empty plane.  An empty plane is one whose band data 
 * has not yet been allocated; a blank plane is one which contains all the necessary
 * band data for a complete page, but which happens to contain no marks.
 *
 * The sample plane structure acts as a template for the band data; all planes must 
 * contain the same number of bands with the same band sizes.
 *
 * If PMS can handle empty planes then you can set the \c uBandTotal to zero and
 * return without creating the blank plane data.
 *
 * \param[in]   ptOILPage         A pointer to the page to receive the blank plane.
 * \param[in]   eColorant         The colorant to be used for the blank plane.
 * \param[in]   ptOILSamplePlane  Used to specify the number and size of bands in the plane.
 *
 */
void CreateOILBlankPlane(OIL_TyPage *ptOILPage, OIL_eColorant eColorant, OIL_TyPlane *ptOILSamplePlane)
{
  unsigned int  size, i;
  unsigned char *ptBuffer;
  int plane;

  /* determine which plane we want to create */
  switch(eColorant)
  {
  case OIL_Red:
  case OIL_Cyan:
    plane = 0;
    break;
  case OIL_Green:
  case OIL_Magenta:
    plane = 1;
    break;
  case OIL_Blue:
  case OIL_Yellow:
    plane = 2;
    break;
  case OIL_Black:
    plane = 3;
    break;
  default:
    HQFAIL("CreateOILBlankPage: Invalid colorant input parameter");
    return;
    break;
  }

  for (i=0; i<ptOILSamplePlane->uBandTotal; i++)
  {
    /* create the band buffer data */
    size = ptOILSamplePlane->atBand[i].cbBandSize;
    ptBuffer = OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, size);
    HQASSERTV(ptBuffer!=NULL,
              ("CreateOILBlankPlane: Failed to allocate %d bytes", size));
    if((ptOILPage->pstJob->eColorMode == OIL_RGB_Separations) || (ptOILPage->pstJob->eColorMode == OIL_RGB_Composite))
    {
      /* For white RGB must be all 1s */
      memset(ptBuffer, 0xFF, size);
    }
    else
    {
      /* For white CMYK must be all 0s */
      memset(ptBuffer, 0x00, size);
    }

    /* fill out the band data */
    ptOILPage->atPlane[plane].atBand[i].pBandRaster = ptBuffer;
    ptOILPage->atPlane[plane].atBand[i].uBandHeight = ptOILSamplePlane->atBand[i].uBandHeight;
    ptOILPage->atPlane[plane].atBand[i].uBandNumber = ptOILSamplePlane->atBand[i].uBandNumber;
    ptOILPage->atPlane[plane].atBand[i].cbBandSize = ptOILSamplePlane->atBand[i].cbBandSize;
  }

  /* fill out the plane data */
  ptOILPage->atPlane[plane].ePlaneColorant = eColorant;
  ptOILPage->atPlane[plane].uBandTotal = ptOILSamplePlane->uBandTotal;
  ptOILPage->atPlane[plane].bBlankPlane = TRUE;

  /* nColorants contains the number of planes created by the RIP.
     If we are creating planes outside the RIP we need to increment the count
     so that OIL has the correct number of planes */
  ptOILPage->nColorants++;
}

/**
 * \brief Create a config test page 
 *
 * This function should creates a pdl string  to generate a config page or pages
 * if more than one page is required it may have to handle  apage at atime, dependent on the 
 * size of the data see the PS and PCL test page exaples for both cases
 *.and then to complete the read cycle when it should return an empty string 
 * \param[in]   sz         A pointer to the buffer for storing the string.
 */
void CreateConfigTestPage(unsigned char *sz)
{
  /* Add code here to generate a config page if required */
    sz[0] = '\0';
    g_pstCurrentJob->eTestPage = OIL_TESTPAGE_NONE;
    g_pstCurrentJob->eTestPage = 0;
}

/**
 * \brief Create a PS Font List test page 
 *
 * This function creates a ps string  to generate a ps fontlist
 *.The function is called once to generate all the pages 
 *.and then to complete the read cycle when it should return an empty string 
 * \param[in]   sz         A pointer to the buffer for storing the string.
 */
void CreatePSTestPage(unsigned char *sz)
{
char *pPaper = SetA4;
char * p = (char *)sz;
PMS_TySystem PMS_tTySystem;
PMS_TyJob *pms_ptJob = NULL;

  if(g_pstCurrentJob->bTestPagesComplete == FALSE)
  {
    PMS_GetJobSettings(&pms_ptJob);
    if(pms_ptJob->tDefaultJobMedia.ePaperSize  == PMS_SIZE_LETTER)
      pPaper = SetLetter;
    PMS_GetSystemInfo(&PMS_tTySystem , PMS_CurrentSettings);

    strcpy((char *)p, fontlistHeader); p += strlen((char *)p) ;
    p += sprintf(p, fontlistUtilities, pPaper,
                                       1, 
                                       PMS_tTySystem.szManufacturer,
                                       PMS_tTySystem.szProduct);
    strcpy((char *)p, fontlistProof); p += strlen((char *)p) ;
    g_pstCurrentJob->bTestPagesComplete = TRUE;
  }
  else
  {
    /* Force the stream read function to return 0 bytes read */
    sz[0] = '\0';
    g_pstCurrentJob->eTestPage = OIL_TESTPAGE_NONE;
  }
}

#ifdef PCLFONTLIST
static int numpages = 0;
/**
 * \brief Create a PCL Font List test page 
 *
 * This function creates a series of pcl command to generate a pcl fontlist
 *.The function is called once for each page required
 *.and then to complete the read cycle when it should return an empty string 
 * \param[in]   sz         A pointer to the buffer for storing the string.
 */
void CreatePCLTestPage(unsigned char *sz)
{
char PCLFontSpec[36];
char fontname[64];
char pagesetup[2][8] = {"\x1b&l2A", "\x1b&l26A"}; /* Letter A4 */
unsigned char * p = sz;
char *pCurrent;
unsigned int i, k, linesperpage, pagesize = 0;
static int index, fontindex, pagenum, TotalFonts;
PMS_TySystem PMS_tTySystem;
PMS_TyJob *pms_ptJob = NULL;

  PMS_GetSystemInfo(&PMS_tTySystem , PMS_CurrentSettings);
#ifdef USE_FF
  TotalFonts = pfin_ff_GetPCLFontTotal();
#endif
#if defined(USE_UFST5) || defined(USE_UFST7)
  TotalFonts = pfin_ufst5_GetPCLFontTotal();
#endif
  if(TotalFonts == 0)
  {
    return;
  }
  /* calculate lines  = actual lines divided by 3 */
  linesperpage = 25;
  /* calculate pages first time through*/
  if(g_pstCurrentJob->bTestPagesComplete == FALSE)
  {
    if(numpages == 0)
    {
      /* only use two sizes for the font list (letter/A4) */
      PMS_GetJobSettings(&pms_ptJob);
      if(pms_ptJob->tDefaultJobMedia.ePaperSize != PMS_SIZE_LETTER)
        pagesize = 1;
      /* start with reset page & set page size */
      strcpy((char *)p, "\33E"); p += strlen((char *)p) ;
      strcpy((char *)p, pagesetup[pagesize]); p += strlen((char *)p) ;
      index = 0;
      fontindex = 0;
      numpages = (TotalFonts + linesperpage -1)/linesperpage;
      pagenum = 0;
    }
    linesperpage = TotalFonts - (pagenum * linesperpage);
    if(linesperpage > 25)
      linesperpage = 25;
    /* write header  and set lines per inch = 8*/
    p += sprintf((char *)p, "\33&a1R\33&a1800H\33&l8D\33(8U\33(s0p8h0s3b4099T%s %s PCL FONT List  %d of %d",
                                                                        PMS_tTySystem.szManufacturer,
                                                                              PMS_tTySystem.szProduct,
                                                                              pagenum +1, numpages);

    for (i = 0; i < linesperpage; i++)
    {
#ifdef USE_FF
      strcpy((char *)PCLFontSpec, pfin_ff_GetPCLFontSpec(fontindex,13, 12, fontname));
#endif
#if defined(USE_UFST5) || defined(USE_UFST7)
      strcpy((char *)PCLFontSpec, pfin_ufst5_GetPCLFontSpec(fontindex,13, 12, fontname));
#endif
      fontindex++;
      /* if the FontSpec is zero length then the font is not available to PCL */
      if(strlen(PCLFontSpec)!= 0)
      {
        /* set the line */
        p += sprintf((char *)p, "\33&a%dR",((i*3) + 5));
        /* set the font id */
        p += sprintf((char *)p, "\33&a360H\33(8U\33(s0p13h0s0b4099T%d",index);
        index++;
        /* set the font name */
        p += sprintf((char *)p, "\33&a720H\33(8U\33(s0p13h0s0b4099T%s",fontname);
        /* select the font and set the column */
        p += sprintf((char *)p, "%s",PCLFontSpec);
        /* print the sample string */
        p += sprintf((char *)p, "\33&a2880HABCDEfghij#$%%';?@[\\^{|}-~123\n");
        /* convert command string to a text string */
        pCurrent = fontname;
        memset(fontname,0,64);
        for(k = 0; k < strlen(PCLFontSpec); k++)
        {
          if(PCLFontSpec[k] == '\33')
          {
            strcat((char *)fontname,"<esc>");
            pCurrent += 5;
          }
          else
          {
            *pCurrent++ = PCLFontSpec[k];
          }
        }
        /* set the command string */
        p += sprintf((char *)p, "\33&a%dR",((i*3) + 6));
        p += sprintf((char *)p, "\33&a720H\33(8U\33(s0p13h0s0b4099T%s",fontname);
      }
      else
      {
        i--;
      }
    }
    /* add a form feed */
    p += sprintf((char *)p, "\14");
    pagenum++;
    if(numpages == pagenum)
    {
      numpages = 0;
      g_pstCurrentJob->bTestPagesComplete = TRUE;
    }
  }
  else
  {
    /* Force the stream read function to return 0 bytes read */
    sz[0] = '\0';
    g_pstCurrentJob->eTestPage = OIL_TESTPAGE_NONE;
  }
}
#undef PCLFONTLIST
#endif

void CreateErrorPage(unsigned char *sz)
{
char *pPaper = SetA4;
char * p = (char *)sz;
char tempBuffer[OIL_MAX_ERRORMESSAGE_LENGTH + 2];
unsigned int i, j;
PMS_TyJob *pms_ptJob = NULL;

  if(g_JobErrorData.bErrorPageComplete == FALSE)
  {
    PMS_GetJobSettings(&pms_ptJob);
    if(pms_ptJob->tDefaultJobMedia.ePaperSize  == PMS_SIZE_LETTER)
      pPaper = SetLetter;
    j = 0;
    for(i = 0; i < strlen(g_JobErrorData.szData); i++)
    {
      /* check to see if the message contains a parenthesis, as this can cause the error page to fail */
      if((g_JobErrorData.szData[i] == '(') || (g_JobErrorData.szData[i] == ')'))
      {
        tempBuffer[j] = '\\';
        j++;
      }
      tempBuffer[j] = g_JobErrorData.szData[i];
      j++;
      if(j > OIL_MAX_ERRORMESSAGE_LENGTH )
        break;
    }
    tempBuffer[j] = '\0';
    p += sprintf(p, pszErrorPage, pPaper,tempBuffer);
    g_JobErrorData.bErrorPageComplete = TRUE;
  }
  else
  {
    /* Force the stream read function to return 0 bytes read */
    sz[0] = '\0';
    g_JobErrorData.Code = 0;
    g_JobErrorData.bErrorPageComplete = FALSE;
  }
}

