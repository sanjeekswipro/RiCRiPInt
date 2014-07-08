/* Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_test.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief Implementation of OIL test features.
 *
 * This file contains functions that provide test facilities inside the OIL.
 *
 * These functions are only intended for use during initial integrations
 * and testing to allow parts of the system to to tested and debugged in isolation. They
 * are not intended to be used within a final implementation.
 *
 * To use these functions, a call must be made from OIL_Start(). The easiest way
 * to do this is to replace OIL_Start() with the following code:
 * <pre>
  int OIL_Start(PMS_TyJob *pms_ptJob )
  {
    GG_SHOW(GG_SHOW_OILVER, "OIL Version: %s - ** TEST ** \n", OILVersion);
    TestCreateRaster(pms_ptJob, OIL_ColorSep);
    return TRUE;
  }
  </pre>
 * (use OIL_ColorSep for color and OIL_Mono for Black and White)
 *
 * TestCreateRasterContents1bpp() can be called to add marks to the raster to allow testing of colorant orders
 * and position on the page.\n
 * There are several helper routines to make marking the raster easier, they are listed below in simplified form.
 * Please refer the the function description of each for a full list of parameters:
 * \arg TestPlot1bpp() - Draw a point at the specified co-ordinates.
 * \arg TestLine1bpp() - Draw a line between the specified co-ordinates.
 * \arg TestFill1bpp() - Fill a rectangular area of the specified size at the specified co-ordinates.
 * \arg TestDrawCornerTicks1bpp() - Draw diagonal tick marks in the corners of the raster area.
 * \arg TestXRuler1bpp() - Draw a horizonal ruler with shorts ticks every 10 pixels and long ticks every 100 pixels.
 *
 */

#include "oil.h"
#include "oil_malloc.h"
#include "oil_interface_skin2oil.h"
#include "oil_interface_oil2pms.h"
#include "oil_test.h"
#include "skinras.h"
#include "oil_job_handler.h"
#include "oil_page_handler.h"
#include "oil_probelog.h"

#include <stdio.h>
#include <string.h>     /* for memset and memcpy */

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;
extern OIL_TyConfigurableFeatures g_NextJobConfiguration;
extern int g_bLogTiming;

/* forward declarations */
static int TestCreateRasterDescription(struct rasterDescription * ptRasterDescription);
static int TestCreateRasterContents1bpp(OIL_TyPage *ptCurrentPage, unsigned int type);
void TestLine1bpp(OIL_TyPage *ptCurrentPage, OIL_eColorant ePlaneColorant, 
                    unsigned int x0, unsigned int x1, unsigned int y0, unsigned int y1, unsigned char uPattern);
void TestPlot1bpp(OIL_TyPage *ptCurrentPage, OIL_eColorant ePlaneColorant, unsigned int x, unsigned int y, unsigned char uPattern);
void TestFill1bpp(OIL_TyPage *ptCurrentPage, OIL_eColorant ePlaneColorant, 
                    unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
void TestDrawCornerTicks1bpp(OIL_TyPage *ptCurrentPage);
void TestXRuler1bpp(OIL_TyPage *ptCurrentPage);


/**
 * \brief Function to create test raster data.
 *
 * This function allows testing of the raster output path through the PMS,
 * without the need for a working input stream or RIP.
 *
 * The function creates a test raster and OIL page structure.  This is then passed
 * into the PMS to simulate a raster output from the RIP.
 * \param[in]   pms_ptJob    A PMS job structure, used to create an OIL job to accompany the raster data.
 * \param[in]   eColorMode   The color mode to use in creating the raster test data.  Valid values are
 *                           defined by OIL_eColorMode.
 * \return      This function always returns TRUE.
 */
int TestCreateRaster (PMS_TyJob *pms_ptJob, OIL_eColorMode eColorMode)
{
  struct rasterDescription * ptRasterDescription;
  static OIL_TyPage *ptCurrentPage;
  PMS_TyPage *ptPMSPage;

  /* create a RIP style raster description */
  ptRasterDescription = (struct rasterDescription *)OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, sizeof(struct rasterDescription));
  HQASSERTV(ptRasterDescription!=NULL,
             ("TestCreateRaster: Failed to allocate %lu bytes",
              (unsigned long) sizeof(struct rasterDescription)));
  TestCreateRasterDescription(ptRasterDescription);

  /* create the OIL job structure */
  g_pstCurrentJob = CreateOILJob(pms_ptJob, OIL_PDL_PS);
  /* override the jobname with a suitable test jobname */
  strcpy(g_pstCurrentJob->szJobName, "TestRaster");

  /* create and OIL style page from the raster description */
  ptCurrentPage = CreateOILPage(ptRasterDescription);

  /* Creating the planes later will set the correct number of colorants, so reset to zero */
  ptCurrentPage->nColorants = 0;

  /* we have control so make a single band per plane, there will always be black */
  ptCurrentPage->atPlane[OIL_Black].uBandTotal = 1;
  ptCurrentPage->atPlane[OIL_Black].atBand[0].uBandNumber = 1;
  ptCurrentPage->atPlane[OIL_Black].atBand[0].uBandHeight = ptCurrentPage->nPageHeightPixels;
  ptCurrentPage->atPlane[OIL_Black].atBand[0].cbBandSize = ptCurrentPage->nPageHeightPixels * ptCurrentPage->nPageWidthPixels;

  /* connect the job data to the page */
  ptCurrentPage->pstJob = g_pstCurrentJob;

  /* connect the page data back to the job */
  g_pstCurrentJob->pPage = ptCurrentPage;

  /* build the raster, use BLACK as the sample since it will always exist */
  CreateOILBlankPlane(ptCurrentPage, OIL_Black, &(ptCurrentPage->atPlane[OIL_Black]));

  /* if not mono then generate other color planes too */
  if (eColorMode != OIL_Mono)
  {
    CreateOILBlankPlane(ptCurrentPage, OIL_Cyan, &(ptCurrentPage->atPlane[OIL_Black]));
    CreateOILBlankPlane(ptCurrentPage, OIL_Magenta, &(ptCurrentPage->atPlane[OIL_Black]));
    CreateOILBlankPlane(ptCurrentPage, OIL_Yellow, &(ptCurrentPage->atPlane[OIL_Black]));
  }

  /* put some data in the page */
  if (ptCurrentPage->uOutputDepth == 1)
  {
    TestCreateRasterContents1bpp(ptCurrentPage, 0);
  }
  else
  {
    GG_SHOW(GG_SHOW_TEST, "Test: data can only be added to raster when output depth is 1bpp\n");
  }


  /* translate the OIL page into a PMS style page in preparation for passing to PMS */
  ptPMSPage = CreatePMSPage(ptCurrentPage);

  /* pass assembled page containing the raster to PMS */
  GGglobal_timing(SW_TRACE_OIL_CHECKIN, 0);
  PMS_CheckinPage(ptPMSPage);

  /* tidy up */
/*  OIL_free(OIL_MemoryPoolA, g_pstCurrentJob); */
/*  OIL_free(OIL_MemoryPoolB, ptCurrentPage); */
  /* TODO : Not tested yet */
  OIL_free(OILMemoryPoolJob, ptRasterDescription);

  return TRUE;
}


/**
 * \brief Test - Create Raster Descriptor
 *
 * A RIP-style raster descriptor structure is populated for use by the test raster data.  This function
 * populates the descriptor with hard-coded values equivalent to A4 paper, 600 dpi, 1 bpp.
 * \param[out]   ptRasterDescription    A pointer to a rasterDescription structure, which will be
 *                                      populated by calling this function
 * \return      This function always returns TRUE.
 */
static int  TestCreateRasterDescription(struct rasterDescription * ptRasterDescription)
{
  /* set to A4 */
  float PageWidth = (float)8.26;
  float PageHeight = (float)11.69;

  /* hardcoded for an 600dpi, 1bpp*/
  ptRasterDescription->xResolution = 600;
  ptRasterDescription->yResolution = 600;
  ptRasterDescription->rasterDepth = 1;

  /* items below this line should not be changed */
  /* To change color mode set OIL_Mono or OIL_ColorSeps in the call to TestCreateRaster() */
  ptRasterDescription->pageNumber = 1;
  ptRasterDescription->imageWidth = (int)(PageWidth * ptRasterDescription->xResolution);
  ptRasterDescription->imageHeight = (int)(PageHeight * ptRasterDescription->yResolution);
  ptRasterDescription->bandHeight = ptRasterDescription->imageHeight;
  ptRasterDescription->dataWidth = (ptRasterDescription->imageWidth + 31) & ~31;
  ptRasterDescription->numChannels = 1;
  ptRasterDescription->nSeparations = 1;
  ptRasterDescription->interleavingStyle = interleavingStyle_frame;
  ptRasterDescription->topMargin = 0;
  ptRasterDescription->bottomMargin = 0;
  ptRasterDescription->leftMargin = 0;
  ptRasterDescription->rightMargin = 0;
  ptRasterDescription->noCopies = 1;
  ptRasterDescription->imageNegate = 0;
  ptRasterDescription->mediaSelect = 0;

  /* set up the page attributes */
  ptRasterDescription->duplex = 0;
  ptRasterDescription->tumble = 0;
  ptRasterDescription->orientation = 0;
  ptRasterDescription->collate = 0;
  ptRasterDescription->outputFaceUp = 0;

  return TRUE;
}

/**
 * \brief Populate the raster contents with 1 bpp data.
 *
 * This function fills the supplied page structure with raster data.
 * \param[out]  ptCurrentPage   The page structure to be populated with raster data.
 * \param[in]   type            Unused parameter.
 * \return      This function always returns TRUE.
 */
static int TestCreateRasterContents1bpp(OIL_TyPage *ptCurrentPage, unsigned int type)
{
  int nPlane;
  unsigned int nPagewidth, nPageHeight, nBoxSize, nShortTick, nLongTick, nBoxXStart;

  UNUSED_PARAM(unsigned int, type);
  /* set constants to use later */
  nPagewidth = ptCurrentPage->nPageWidthPixels;
  nPageHeight = ptCurrentPage->nPageHeightPixels;
  nBoxSize = nPagewidth/10;
  nBoxXStart = (nPagewidth/2)-(2*nBoxSize);
  nShortTick = nPagewidth/5;
  nLongTick = nPagewidth/3;

  if (ptCurrentPage->nColorants == 1)
  {
    /* mono */
    TestDrawCornerTicks1bpp(ptCurrentPage);
    TestXRuler1bpp(ptCurrentPage);
  }
  else
  {
    /* color */
    TestDrawCornerTicks1bpp(ptCurrentPage);
    TestXRuler1bpp(ptCurrentPage);

    for (nPlane = 0; nPlane < ptCurrentPage->nColorants; nPlane++)
    {
      switch (nPlane)
      {
      case OIL_Black:
        TestFill1bpp(ptCurrentPage, OIL_Black, nBoxXStart+3*nBoxSize, (nPageHeight-nBoxSize)/2, nBoxSize, nBoxSize);
        break;
      case OIL_Cyan:
        TestFill1bpp(ptCurrentPage, OIL_Cyan, nBoxXStart, (nPageHeight-nBoxSize)/2, nBoxSize, nBoxSize);
        break;
      case OIL_Magenta:
        TestFill1bpp(ptCurrentPage, OIL_Magenta, nBoxXStart+nBoxSize, (nPageHeight-nBoxSize)/2, nBoxSize, nBoxSize);
        break;
      case OIL_Yellow:
        TestFill1bpp(ptCurrentPage, OIL_Yellow, nBoxXStart+2*nBoxSize, (nPageHeight-nBoxSize)/2, nBoxSize, nBoxSize);
        break;
      }
    }
  }
  return TRUE;
}

/**
 * \brief Draw a line on a page.
 *
 * A line of specified colorant is dramwn between (x0, y0) and (x1, y1).
 * The method used is Bresenham's algorithm.
 * The pattern parameter sets the line pattern (0xff = solid, 0xf0 = dashed etc)
 * \param[in,out] ptCurrentPage   A page structure. The line will be drawn onto this page.
 * \param[in]     ePlaneColorant  The colorant to be used to draw the line.
 * \param[in]     x0              The x co-ordinate from which the line starts.
 * \param[in]     y0              The y co-ordinate from which the line starts.
 * \param[in]     x1              The x co-ordinate at which the line ends.
 * \param[in]     y1              The y co-ordinate at which the line ends.
 * \param[in]     uPattern        The bit pattern of this integer sets the line pattern.
 *                                For example, 0xff = solid, 0xf0 = dashed and so on.
 */
void TestLine1bpp(OIL_TyPage *ptCurrentPage, OIL_eColorant ePlaneColorant, 
                    unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, unsigned char uPattern)
{
  unsigned int steep;
  int deltax, deltay, error, ystep, swaptmp;
  unsigned int x, y;
  unsigned int mask, nPatternShift;

  steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep)
  {
    swaptmp = x0; x0 = y0; y0 = swaptmp; /* swap(x0, y0) */
    swaptmp = x1; x1 = y1; y1 = swaptmp; /* swap(x1, y1) */
  }
  if (x0 > x1)
  {
   swaptmp = x0; x0 = x1; x1 = swaptmp; /* swap(x0, x1) */
   swaptmp = y0; y0 = y1; y1 = swaptmp; /* swap(y0, y1) */
  }
  deltax = x1 - x0;
  deltay = abs(y1 - y0);
  error = -deltax / 2;
  y = y0;
  if (y0 < y1)
  {
    ystep = 1;
  }
  else
  {
    ystep = -1;
  }
  for (x=x0; x<x1; x++)
  {
    nPatternShift = x%8;
    mask = (uPattern >> nPatternShift) & 0x01;
    if (steep)
    {
      TestPlot1bpp(ptCurrentPage, ePlaneColorant, y, x, (unsigned char)mask);
    }
    else
    {
      TestPlot1bpp(ptCurrentPage, ePlaneColorant, x, y, (unsigned char)mask);
    }
    error = error + deltay;
    if (error > 0)
    {
      y = y + ystep;
      error = error - deltax;
    }
  }
}

/**
 * \brief Draw a filled box.
 *
 * A filled box of specified colorant is drawn at (x0, y0) with size (x1, y1).\n
 * NOTE: size must be positive.
 * \param[in,out] ptCurrentPage   A page structure. The box will be drawn onto this page.
 * \param[in]     ePlaneColorant  The colorant to be used to draw the box.
 * \param[in]     x0              The x co-ordinate of the box's origin.
 * \param[in]     y0              The y co-ordinate of the box's origin.
 * \param[in]     x1              The x-size of the box.
 * \param[in]     y1              The-size of the box.
 */
void TestFill1bpp(OIL_TyPage *ptCurrentPage, OIL_eColorant ePlaneColorant, 
                    unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1)
{
  unsigned int yfill;

  for (yfill = y0; yfill<(y0+y1); yfill++)
  {
    TestLine1bpp(ptCurrentPage, ePlaneColorant, x0, yfill, x0+x1, yfill, 0xff);
  }
}

/**
 * \brief Draw a ruler.
 *
 * This function places a black horizontal line onto the page, with short tick marks every 
 * 10 pixels, and long tick marks every 100 pixels.
 *
 * \param[in,out] ptCurrentPage   A page structure. The ruler will be drawn onto this page.
 */
void TestXRuler1bpp(OIL_TyPage *ptCurrentPage)
{
  int x;
  unsigned int nYPosition, nShortTick, nLongTick;

  nYPosition = ptCurrentPage->nPageHeightPixels/3;
  nShortTick = ptCurrentPage->nPageHeightPixels/200;
  nLongTick = ptCurrentPage->nPageHeightPixels/100;

  /*draw line */
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, nYPosition, ptCurrentPage->nPageWidthPixels, nYPosition, 0xff);

  /* draw ticks - small every 10, large every 100 */
  for (x=0; x<ptCurrentPage->nPageWidthPixels; x++)
  {
    if ((x%10)==0)
    {
      /* small tick */
      TestLine1bpp(ptCurrentPage, OIL_Black, x, nYPosition, x, nYPosition+nShortTick, 0xff);
    }
    if ((x%100)==0)
    {
      /* large tick */
      TestLine1bpp(ptCurrentPage, OIL_Black, x, nYPosition, x, nYPosition+nLongTick, 0xff);
    }
  }
}


/**
 * \brief Draw corner ticks.
 *
 * Four black tick marks are drawn with their points to the exact corner of the valid raster.
 * The inner ticks marks are 2 pixels wide (2 lines drawn next to each other, the
 * outer marks are single pixel lines.
 *
 * \param[in,out] ptCurrentPage   A page structure. The corner ticks will be drawn onto this page.
 */
void TestDrawCornerTicks1bpp(OIL_TyPage *ptCurrentPage)
{
  unsigned int nPagewidth, nPageHeight, nBoxSize, nShortTick, nLongTick, nBoxXStart;

  /* set constants to use later (sub 1 since these are used in calculations */
  nPagewidth = ptCurrentPage->nPageWidthPixels - 1;
  nPageHeight = ptCurrentPage->nPageHeightPixels - 1;
  nBoxSize = nPagewidth/10;
  nBoxXStart = (nPagewidth/2)-(2*nBoxSize);

  /* 45 degree -  diagonal tick marks */
  nLongTick = nPagewidth/3;
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, 0, nLongTick, nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, 0, nPagewidth-nLongTick, nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, nPageHeight, nLongTick, nPageHeight-nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, nPageHeight, nPagewidth-nLongTick, nPageHeight-nLongTick, 0xff);

  /* Thick (2 pixels) ticks - draw each line twice, 1 pixel apart*/
  nShortTick = nPagewidth/5;
  nLongTick = nPagewidth/3;
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, 0, nShortTick, nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, 1, nShortTick, nLongTick+1, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, 0, nLongTick, nShortTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, 1, nLongTick, nShortTick+1, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, 0, nPagewidth-nLongTick, nShortTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, 1, nPagewidth-nLongTick, nShortTick+1, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, 0, nPagewidth-nShortTick, nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, 1, nPagewidth-nShortTick, nLongTick+1, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, nPageHeight, nLongTick, nPageHeight-nShortTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, nPageHeight-1, nLongTick, nPageHeight-nShortTick+1, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, nPageHeight, nShortTick, nPageHeight-nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, nPageHeight-1, nShortTick, nPageHeight-nLongTick+1, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, nPageHeight, nPagewidth-nLongTick, nPageHeight-nShortTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, nPageHeight-1, nPagewidth-nLongTick, nPageHeight-nShortTick+1, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, nPageHeight, nPagewidth-nShortTick, nPageHeight-nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, nPageHeight-1, nPagewidth-nShortTick, nPageHeight-nLongTick+1, 0xff);

  /* thin (1 pixel) outer ticks */
  nShortTick = nPagewidth/10;
  nLongTick = nPagewidth/3;
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, 0, nShortTick, nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, 0, nLongTick, nShortTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, 0, nPagewidth-nLongTick, nShortTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, 0, nPagewidth-nShortTick, nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, nPageHeight, nLongTick, nPageHeight-nShortTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, 0, nPageHeight, nShortTick, nPageHeight-nLongTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, nPageHeight, nPagewidth-nLongTick, nPageHeight-nShortTick, 0xff);
  TestLine1bpp(ptCurrentPage, OIL_Black, nPagewidth, nPageHeight, nPagewidth-nShortTick, nPageHeight-nLongTick, 0xff);
}

/**
 * \brief Test - Plot a point 
 *
 * A point of specified colorant is drawn at (x, y)
 *
 * \param[in,out] ptCurrentPage   A page structure. The point will be drawn onto this page.
 * \param[in]     ePlaneColorant  The colorant to be used to draw the point.
 * \param[in]     x               The x co-ordinate of the point.
 * \param[in]     y               The y co-ordinate of the point.
 * \param[in]     data            Specifies whether or not...
 * \todo don't know what 'data' does...
 */
void TestPlot1bpp(OIL_TyPage *ptCurrentPage, OIL_eColorant ePlaneColorant, unsigned int x, unsigned int y, unsigned char data)
{
  unsigned int nBytesPerLine;
  unsigned int byte;

  unsigned char *pStart;

  nBytesPerLine = (ptCurrentPage->nPageWidthPixels +7)/ 8;
  /* find the corrrect line */
  pStart = ptCurrentPage->atPlane[ePlaneColorant].atBand[0].pBandRaster + (y*(ptCurrentPage->nRasterWidthData/8)) + x/8;
  /* select bitlane */
  byte = *pStart;

  if (data == 1)
  {
    /* set */
    *pStart = (unsigned char)(byte | (0x80 >> x%8));
  }
  else
  {
    /* clear */
    *pStart = (unsigned char)(byte & ~(0x80 >> x%8));
  }
}

/**
 * \brief Function to test the job setup configuration.
 *  
 * This function can only configure the paper selection method, the shutdown option
 * and the flags which configure which messages will be shown.
 * All parameters set here must be copied into current configuration in JobExit().
 *
 * Example: 
 *   3 GG_SetTestConfiguration -> Sets paper size selection method to "from job".
 * \param[in]   Config      Selects the configuration option to be set in the call.
 */
void TestSetupConfiguration(int Config)
{
  switch(Config)
  {
  case 3:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: Paper selection = NONE\n");
    g_NextJobConfiguration.g_ePaperSelectMode = OIL_PaperSelNone;
    break;
  case 4:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: Paper selection = RIP\n");
    g_NextJobConfiguration.g_ePaperSelectMode = OIL_PaperSelRIP;
    break;
  case 5:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: Paper selection = PMS\n");
    g_NextJobConfiguration.g_ePaperSelectMode = OIL_PaperSelPMS;
    break;
  case 6:
    /* this is not applicable to OIL singlethreaded, the option will be ignored */
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: Shutdown selection = Shutdown for the last job\n");
    g_NextJobConfiguration.g_eShutdownMode = OIL_RIPShutdownPartial;
    break;
  case 7:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: Shutdown selection = Shutdown for every job\n");
    g_NextJobConfiguration.g_eShutdownMode = OIL_RIPShutdownTotal;
    break;
#ifdef GG_DEBUG
  case 100:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: Disable all debugging messages\n");
    g_NextJobConfiguration.g_uGGShow = 0;
    break;
  case 101:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_MEMORY\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_MEMORY;
    break;
  case 102:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_CMM\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_CMM;
    break;
  case 103:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_SCREENING\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_SCREENING;
    break;
  case 104:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_FONTS\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_FONTS;
    break;
  case 105:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_OIL\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_OIL;
    break;
  case 106:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_EBDDEV\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_EBDDEV;
    break;
  case 107:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_MEDIA\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_MEDIA;
    break;
  case 108:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_THREADING\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_THREADING;
    break;
  case 109:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_PSCONFIG\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_PSCONFIG;
    break;
  case 110:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_TEST\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_TEST;
    break;
  case 111:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_1BPP\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_1BPP;
    break;
  case 112:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_TIMING\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_TIMING;
    break;
  case 113:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_OILVER\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_OILVER;
    break;
  case 114:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: GG_SHOW_COMMENTPARSER\n");
    g_NextJobConfiguration.g_uGGShow = g_NextJobConfiguration.g_uGGShow | GG_SHOW_COMMENTPARSER;
    break;
#endif
  case 115:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: Turn on timing dumplog\n");
    g_bLogTiming = TRUE;
    g_ConfigurableFeatures.g_uGGShow = g_NextJobConfiguration.g_uGGShow |= GG_SHOW_TIMING;
    break;
  default:
    GG_SHOW(GG_SHOW_TEST, "TestSetupConfiguration: unknown action - %d\n", Config);
    break;
  }
}

