/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_tiff_out.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief TIFF Output Handler.
 *
 * These routines emulate outputting the band data to the output (which is in this case - tiff file)
 */

#include "gge_tiff.h"
#include "pms.h"
#include "pms_malloc.h"
#include "pms_tiff_out.h"
#include <stdio.h>
#include <string.h>

/* \brief Macro to limit maximum possible characters in output filename*/
#define LONGESTFILENAME 256

/**
 * \brief Creates a TIFF file for the page data.
 *
 * Description: Creates a TIFF image file.\n
 * Input: single PMS_TyPage page structure containing image raster in bands.\n
 * Return Value: TRUE if successful, FALSE otherwise.
 */
int TIFF_PageHandler( PMS_TyPage *ptPMSPage )
{
  TGGE_TIFF_HEADER tMyTIFFHeader;
  TGGE_TIFF_BAND_COMPOSITE tMyTIFFCompBand;
  TGGE_TIFF_BAND_SEPARATED tMyTIFFSepBand;
  TGGE_TIFF_BAND_PIXEL_INTERLEAVED tMyTIFFPixelInterleaved;
  unsigned char szImageDescription[2048];
  char szRasterFilename[ LONGESTFILENAME ];
  int thisColor, x, nCopyNo;
  PMS_eTIFF_Errors nResult = TIFF_NoError;
  unsigned int i, j, y;
  static unsigned int nCurrentJobID = 0, nPageNo = 0;
  char *pDst,pDestFile[PMS_MAX_OUTPUTFOLDER_LENGTH];

  /* checks for features not supported */
  switch (ptPMSPage->uOutputDepth) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
      break;
    default:
      PMS_FAIL("Raster depth (%d) not yet supported by pms_tiff_out.c\n", ptPMSPage->uOutputDepth);
      return TIFF_Error_bppNotSupported;
  }

  /* 1 and 2 bpp RGB is not yet supported */
  if((ptPMSPage->uTotalPlanes == 3) && (ptPMSPage->uOutputDepth < 8))
  {
    PMS_SHOW_ERROR("\n **** ASSERT ****** \n1, 2 and 4 bpp RGB is not yet supported in TIFF out.\nFailed to output TIFF.\n\n");
    return TIFF_Error_bppNotSupported;
  }

  /* if its a new job reset page numbering */
  if(nCurrentJobID != ptPMSPage->JobId)
  {
    nCurrentJobID = ptPMSPage->JobId;
    nPageNo = 1;
  }
  memset(pDestFile,0,PMS_MAX_OUTPUTFOLDER_LENGTH);

  if(g_tSystemInfo.szOutputPath[0])
  {
    memcpy(pDestFile,g_tSystemInfo.szOutputPath,strlen(g_tSystemInfo.szOutputPath));
    pDst=strrchr(ptPMSPage->szJobName,'/');
    strcat(pDestFile,"/");
    if(pDst)
    {
      strcat(pDestFile,pDst+1);
    }
    else
    {
      strcat(pDestFile,ptPMSPage->szJobName);
    }
  }
  else
  {
    memcpy(pDestFile,ptPMSPage->szJobName,strlen(ptPMSPage->szJobName));
  }

  sprintf(szRasterFilename, "%s%d-%d", pDestFile, ptPMSPage->JobId, nPageNo);

  /* Fill in image info from raster description */
  memset(&tMyTIFFHeader, 0x00, sizeof(tMyTIFFHeader));
  tMyTIFFHeader.cbSize = sizeof(tMyTIFFHeader);
  tMyTIFFHeader.pszDescription = &szImageDescription[0];
  tMyTIFFHeader.uColorants = ptPMSPage->uTotalPlanes;
  tMyTIFFHeader.uBitsPerPixel = ptPMSPage->uOutputDepth;
  tMyTIFFHeader.uBytesPerLine = ptPMSPage->nRasterWidthBits/8;
  tMyTIFFHeader.uBytesPerLine = ((tMyTIFFHeader.uBytesPerLine + 3)&~3); /* round up to four bytes (32 bits) */
  tMyTIFFHeader.uLinesPerPage = ptPMSPage->nPageHeightPixels;
  if (ptPMSPage->eColorMode == PMS_RGB_PixelInterleaved) {
    tMyTIFFHeader.uWidthPixels = ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth / 3;
  } else {
    tMyTIFFHeader.uWidthPixels = ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth;
  }
  tMyTIFFHeader.uXDPI = (unsigned int)(ptPMSPage->dXResolution + 0.5);
  tMyTIFFHeader.uYDPI = (unsigned int)(ptPMSPage->dYResolution + 0.5);
  if((ptPMSPage->uTotalPlanes == 1) || (g_tSystemInfo.eOutputType == PMS_TIFF_SEP))
    tMyTIFFHeader.bSeparated = 1;
  else
    tMyTIFFHeader.bSeparated = 0;
  if ( ptPMSPage->uOutputDepth < 4 ) /* for 1bpp and 2bpp use RGB palette for reduced tiff size*/
    tMyTIFFHeader.bRGBPal = 1;
  else
    tMyTIFFHeader.bRGBPal = 0;

  /* set this to 1 if Bytes in a word need swapping */
  tMyTIFFHeader.bSwapBytesInWord = 0;

  /* fill TIFF's comment buffer */
  sprintf((char *) tMyTIFFHeader.pszDescription,
      "%s    \r\n"
      "Colorants: %d\r\n"
      "RIP Depth (bpp): %d\r\n"
      "Output Depth (bpp): %d\r\n"
      "Pixels Per Line (not inc. padding): %d\r\n"
      "Bytes Per Line (inc. padding): %d\r\n"
      "Bits Per Line (inc. padding): %d\r\n"
      "Lines Per Page: %d\r\n"
      "Job: %d Page: %d\r\n"
      "JobName: %s\r\n"
      "X dpi: %.2f\r\n"
      "Y dpi: %.2f\r\n"
      "Color Mode: %d\r\n"
      "Screen Mode: %d\r\n"
      "TotalPlanes: %d\r\n"
      "TotalBands: %d\r\n"
      "MediaType: %s\r\n"
      "MediaColor: %s\r\n"
      "MediaWeight: %d\r\n"
      "MediaSource: %d\r\n"
      "MediaWidth: %3.3f inches\r\n"
      "MediaHeight: %3.3f inches\r\n"
      "Duplex: %d\r\n"
      "Tumble: %d\r\n"
      "Collate: %d\r\n"
      "OutputFaceUp: %d\r\n"
      "Orientation: %d\r\n"
      "Output Tray: %d\r\n"
      ,
      (char*)szRasterFilename,
      ptPMSPage->uTotalPlanes,
      ptPMSPage->uRIPDepth,
      ptPMSPage->uOutputDepth,
      ptPMSPage->nPageWidthPixels,
      tMyTIFFHeader.uBytesPerLine,
      tMyTIFFHeader.uBytesPerLine * 8,
      ptPMSPage->nPageHeightPixels,
      ptPMSPage->JobId,
      ptPMSPage->PageId,
      ptPMSPage->szJobName,
      ptPMSPage->dXResolution,
      ptPMSPage->dYResolution,
      ptPMSPage->eColorMode,
      ptPMSPage->eScreenMode,
      ptPMSPage->uTotalPlanes,
      ptPMSPage->atPlane[3].uBandTotal,
      ptPMSPage->stMedia.szMediaType,
      ptPMSPage->stMedia.szMediaColor,
      ptPMSPage->stMedia.uMediaWeight,
      ptPMSPage->stMedia.uInputTray,
      ptPMSPage->stMedia.dWidth/72,
      ptPMSPage->stMedia.dHeight/72,
      ptPMSPage->bDuplex,
      ptPMSPage->bTumble,
      ptPMSPage->bCollate,
      ptPMSPage->bFaceUp,
      ptPMSPage->uOrientation,
      ptPMSPage->stMedia.eOutputTray
      );

  /* fill command line arguments */
  strcat((char*)tMyTIFFHeader.pszDescription, "Command line arguments: ");
  for(i=1; i < (unsigned int)g_argc; i++)
  {
    /* minus 6 to allow space for "...\r\n\0" if required */
    if( (strlen(g_argv[i]) + strlen((char*)tMyTIFFHeader.pszDescription)) < (sizeof(szImageDescription) - 6) )
    {
        strcat((char*)tMyTIFFHeader.pszDescription, g_argv[i]);
        strcat((char*)tMyTIFFHeader.pszDescription, " ");
    }
    else
    {
        strcat((char*)tMyTIFFHeader.pszDescription, "...");
        break;
    }
  }
  strcat((char*)tMyTIFFHeader.pszDescription, "\r\n");


  for(nCopyNo = 1; nCopyNo <= ptPMSPage->nCopies; nCopyNo++)
  {
    sprintf(szRasterFilename, "%s%d-%d", pDestFile, ptPMSPage->JobId, nPageNo);
    tMyTIFFHeader.pszPathName = (char*)szRasterFilename;

    strncpy((char*)tMyTIFFHeader.pszDescription, (char*)szRasterFilename, strlen((char*)szRasterFilename));

    nResult = GGE_TIFF(GGE_TIFF_INSEL_HEADER, &tMyTIFFHeader);
    if(nResult != GGE_TIFF_SUCCESS)
    {
      PMS_SHOW_ERROR("\nTIFF_PageHandler: Failed to output tiff header. Return value(%d)\n", nResult);
      return TIFF_Error_GGETiff;
    }

    if((ptPMSPage->uTotalPlanes == 1) || (g_tSystemInfo.eOutputType == PMS_TIFF_SEP)) /* MonoChrome job handling and Separated CMYK job handling */
    {
      memset(&tMyTIFFSepBand, 0x00, sizeof(tMyTIFFSepBand));
      tMyTIFFSepBand.cbSize = sizeof(tMyTIFFSepBand);
      for (i = 0; i < ptPMSPage->uTotalPlanes; i++)
      {
        /* start with Black because if there is only 1 plane it will be only black */
        if(ptPMSPage->uTotalPlanes == 1)
          thisColor = 3;
        else
          thisColor = i;
        for(j=0; j < ptPMSPage->atPlane[thisColor].uBandTotal; j++)
        {
          tMyTIFFSepBand.pBandBuffer = (char*)ptPMSPage->atPlane[thisColor].atBand[j].pBandRaster;
          tMyTIFFSepBand.uColorant = i;
          tMyTIFFSepBand.uLinesThisBand = ptPMSPage->atPlane[thisColor].atBand[j].uBandHeight;

          /* 2bpp and 4bpp are done by the screening modules in oil... however, the
             modules do not clear the padding pixels, and nor does the rip.
             The engine may not care about the padded data... but it will appear
             in the TIFF output, so we will clear here (GGETIFF does not know how many
             pixels are in the padded part). For 1 bpp also we need to clear the padding*/
          if(ptPMSPage->uOutputDepth == 2)
          {
            pDst = tMyTIFFSepBand.pBandBuffer + ((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) >> 3);
            for(y = 0; y < tMyTIFFSepBand.uLinesThisBand; y++)
            {
              for(x = ptPMSPage->nPageWidthPixels;
                  x < (ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth);
                  )
              {
                switch(x%4)
                {
                default:
                case 0:
                  *pDst = 0x00;
                  /* set padding to light grey... *pDst = 0x55; */
                  x+=4;
                  pDst++;
                  break;
                case 1:
                  *pDst &= 0xC0;
                  /* set padding to light grey... *pDst |= 0x15; */
                  x+=3;
                  pDst++;
                  break;
                case 2:
                  *pDst &= 0xF0;
                  /* set padding to light grey... *pDst |= 0x05; */
                  x+=2;
                  pDst++;
                  break;
                case 3:
                  *pDst &= 0xFC;
                  /* set padding to light grey... *pDst |= 0x01; */
                  x+=1;
                  pDst++;
                  break;
                }
              }
              pDst += ((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) >> 3);
            }
          } 
          else if(ptPMSPage->uOutputDepth == 4)
          {
            pDst = tMyTIFFSepBand.pBandBuffer + ((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) >> 3);
            for(y = 0; y < tMyTIFFSepBand.uLinesThisBand; y++)
            {
              for(x = ptPMSPage->nPageWidthPixels;
                  x < (ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth);
                  )
              {
                if(x%2)
                {
                  *pDst &= 0xF0;
                  /* set padding to light grey... *pDst |= 0x01; */
                  x++;
                }
                else
                {
                  *pDst = 0x00;
                  /* set padding to light grey... *pDst = 0x11; */
                  x+=2;
                }
                pDst++;
              }
              pDst += ((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) >> 3);
            }
          } 
          else if(ptPMSPage->uOutputDepth == 1) /* For 1 bpp also we need to clear the padding*/
          {
            int nPadStartByte = ptPMSPage->nPageWidthPixels >> 3;
            int nBytesWithPadding = ptPMSPage->nRasterWidthBits >> 3;
            int nBytesToSwap = (((nBytesWithPadding - nPadStartByte) + 3) >> 2) << 2; /* multiple of 4 bytes*/
            int nDataBytes;
            pDst = tMyTIFFSepBand.pBandBuffer + (nBytesWithPadding - nBytesToSwap);

            for(y = 0; y < tMyTIFFSepBand.uLinesThisBand; y++)
            {
#ifdef PMS_LOWBYTEFIRST
              /* in little-endian case, we first need to byte swap the 32-bit word,
              clear padding and then undo the swap */
              unsigned int *pTempBuffer = (unsigned int *)pDst ;
              int nSwapIterations = nBytesToSwap/4;
              while(nSwapIterations--)
              {
                *pTempBuffer = (*pTempBuffer << 24) | ((*pTempBuffer & 0xff00) << 8) | ((*pTempBuffer & 0xff0000) >> 8) | (*pTempBuffer >> 24);
                pTempBuffer++;
              }
              /* Hold the starting address of the swapped bytes. Will be needed later to unswap */
              pTempBuffer = (unsigned int *)pDst ;
#endif
              nDataBytes = nPadStartByte - (nBytesWithPadding - nBytesToSwap);
              /* nDataBytes = number of bytes in nBytesToSwap that do not contain padded bits
                 no need to do any processing for these bytes */
              while(nDataBytes--)
              {
                pDst++;
              }

              /* clear only the padded bits starting from the end of valid data */
              for(x = ptPMSPage->nPageWidthPixels; x < (ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth);)
              {
                switch(x%8)
                {
                default:
                case 0:
                  *pDst = 0x00;
                  /* set padding to black... (for testing)
                  *pDst = 0xFF; */
                  x+=8;
                  pDst++;
                  break;
                case 1:
                  *pDst &= 0x80;
                  /* set padding to black... (for testing)
                  *pDst |= 0x7F; */
                  x+=7;
                  pDst++;
                  break;
                case 2:
                  *pDst &= 0xC0;
                  /* set padding to black... (for testing)
                  *pDst |= 0x3F; */
                  x+=6;
                  pDst++;
                  break;
                case 3:
                  *pDst &= 0xE0;
                  /* set padding to black... (for testing)
                  *pDst |= 0x1F; */
                  x+=5;
                  pDst++;
                  break;
                case 4:
                  *pDst &= 0xF0;
                  /* set padding to black... (for testing)
                  *pDst |= 0x0F; */
                  x+=4;
                  pDst++;
                  break;
                case 5:
                  *pDst &= 0xF8;
                  /* set padding to black... (for testing)
                  *pDst |= 0x07; */
                  x+=3;
                  pDst++;
                  break;
                case 6:
                  *pDst &= 0xFC;
                  /* set padding to black... (for testing)
                  *pDst |= 0x03; */
                  x+=2;
                  pDst++;
                  break;
                case 7:
                  *pDst &= 0xFE;
                  /* set padding to black... (for testing)
                  *pDst |= 0x01; */
                  x+=1;
                  pDst++;
                  break;
                }
              }
#ifdef PMS_LOWBYTEFIRST
              /* in little-endian case, we swapped the 32-bit word before clearing the padded bits above,
              We must now undo the swap */
              nSwapIterations = nBytesToSwap/4;
              while(nSwapIterations--)
              {
                *pTempBuffer = (*pTempBuffer << 24) | ((*pTempBuffer & 0xff00) << 8) | ((*pTempBuffer & 0xff0000) >> 8) | (*pTempBuffer >> 24);
                pTempBuffer++;
              }
#endif
              /* Advance to next line in the raster */
              pDst += (nBytesWithPadding - nBytesToSwap);
            }
          } /* ptPMSPage->uOutputDepth == 1 */

          nResult = GGE_TIFF(GGE_TIFF_INSEL_BAND_SEPARATED, &tMyTIFFSepBand);
          if(nResult != GGE_TIFF_SUCCESS)
          {
            PMS_SHOW_ERROR("\nTIFF_PageHandler: Failed to output tiff Separated Band. Return value(%d)\n", nResult);
            return TIFF_Error_GGETiff;
          }
        }
      }
    } /* end of Separated CMYK job handling */
    else if (ptPMSPage->eColorMode == PMS_RGB_PixelInterleaved)
    {
      memset(&tMyTIFFPixelInterleaved, 0x00, sizeof(tMyTIFFPixelInterleaved));
      tMyTIFFPixelInterleaved.cbSize = sizeof(tMyTIFFPixelInterleaved);

      for(j=0; j < ptPMSPage->atPlane[0].uBandTotal; j++)
      {
        tMyTIFFPixelInterleaved.uLinesThisBand = ptPMSPage->atPlane[0].atBand[j].uBandHeight;
        tMyTIFFPixelInterleaved.pBandBuffer = (char*)ptPMSPage->atPlane[0].atBand[j].pBandRaster;
        nResult = GGE_TIFF(GGE_TIFF_INSEL_BAND_PIXEL_INTERLEAVED, &tMyTIFFPixelInterleaved);
      }
    }
  else /* CMYK/RGB Composite Band color job handling */
  {
    memset(&tMyTIFFCompBand, 0x00, sizeof(tMyTIFFCompBand));
    tMyTIFFCompBand.cbSize = sizeof(tMyTIFFCompBand);
    tMyTIFFCompBand.paBandBuffer = (char **)OSMalloc(sizeof(char *) * ptPMSPage->uTotalPlanes,PMS_MemoryPoolPMS);
    if(!tMyTIFFCompBand.paBandBuffer)
    {
      PMS_SHOW_ERROR("\n ***ASSERT*** TIFF_PageHandler: Failed to allocate %u bytes of memory for composite band structure.\n", (unsigned int)sizeof(char *) * ptPMSPage->uTotalPlanes);
      return TIFF_Error_Memory;
    }
    tMyTIFFCompBand.uColorants = ptPMSPage->uTotalPlanes;

      /* assumes all planes have same number of bands */
      for(j=0; j < ptPMSPage->atPlane[0].uBandTotal; j++)
      {
        /* assumes all bands across planes are same height */
        tMyTIFFCompBand.uLinesThisBand = ptPMSPage->atPlane[0].atBand[j].uBandHeight;

        for (i = 0; i < ptPMSPage->uTotalPlanes; i++)
        {
          /* set band pointer... */
          tMyTIFFCompBand.paBandBuffer[i] = (char*)ptPMSPage->atPlane[i].atBand[j].pBandRaster;

          /* 2bpp and 4bpp are done by the screening modules in oil... however, the
             modules do not clear the padding pixels, and nor does the rip.
             The engine may not care about the padded data... but it will appear
             in the TIFF output, so we will clear here (GGETIFF does not know how many
             pixels are in the padded part). For 1 bpp also we need to clear the padding*/
          if(ptPMSPage->uOutputDepth == 2)
          {
            pDst = tMyTIFFCompBand.paBandBuffer[i] + ((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) >> 3);
            for(y = 0; y < tMyTIFFCompBand.uLinesThisBand; y++)
            {
              for(x = ptPMSPage->nPageWidthPixels;
                  x < (ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth);
                  )
              {
                switch(x%4)
                {
                default:
                case 0:
                  *pDst = 0x00;
                  /* set padding to light grey... *pDst = 0x55; */
                  x+=4;
                  pDst++;
                  break;
                case 1:
                  *pDst &= 0xC0;
                  /* set padding to light grey... *pDst |= 0x15; */
                  x+=3;
                  pDst++;
                  break;
                case 2:
                  *pDst &= 0xF0;
                  /* set padding to light grey... *pDst |= 0x05; */
                  x+=2;
                  pDst++;
                  break;
                case 3:
                  *pDst &= 0xFC;
                  /* set padding to light grey... *pDst |= 0x01; */
                  x+=1;
                  pDst++;
                  break;
                }
              }
              pDst += ((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) >> 3);
            }
          } 
          else if(ptPMSPage->uOutputDepth == 4)
          {
            pDst = tMyTIFFCompBand.paBandBuffer[i] + ((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) >> 3);
            for(y = 0; y < tMyTIFFCompBand.uLinesThisBand; y++)
            {
            for(x = ptPMSPage->nPageWidthPixels;
                x < (ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth);
                )
            {
                if(x%2)
                {
                  *pDst &= 0xF0;
                  /* set padding to light grey... *pDst |= 0x01; */
                  x++;
                }
                else
                {
                  *pDst = 0x00;
                  /* set padding to light grey... *pDst = 0x11; */
                  x+=2;
                }
                pDst++;
              }
              pDst += ((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) >> 3);
            }
          } 
          else if(ptPMSPage->uOutputDepth == 1) /* For 1 bpp also we need to clear the padding*/
          {
            int nPadStartByte = ptPMSPage->nPageWidthPixels >> 3;
            int nBytesWithPadding = ptPMSPage->nRasterWidthBits >> 3;
            int nBytesToSwap = (((nBytesWithPadding - nPadStartByte) + 3) >> 2) << 2; /* multiple of 4 bytes*/
            int nDataBytes;
            pDst = tMyTIFFCompBand.paBandBuffer[i] + (nBytesWithPadding - nBytesToSwap);

            for(y = 0; y < tMyTIFFCompBand.uLinesThisBand; y++)
            {
#ifdef PMS_LOWBYTEFIRST
              /* in little-endian case, we first need to byte swap the 32-bit word,
              clear padding and then undo the swap */
              unsigned int *pTempBuffer = (unsigned int *)pDst ;
              int nSwapIterations = nBytesToSwap/4;
              while(nSwapIterations--)
              {
                *pTempBuffer = (*pTempBuffer << 24) | ((*pTempBuffer & 0xff00) << 8) | ((*pTempBuffer & 0xff0000) >> 8) | (*pTempBuffer >> 24);
                pTempBuffer++;
              }
              /* Hold the starting address of the swapped bytes. Will be needed later to unswap */
              pTempBuffer = (unsigned int *)pDst ;
#endif
              nDataBytes = nPadStartByte - (nBytesWithPadding - nBytesToSwap);
              /* nDataBytes = number of bytes in nBytesToSwap that do not contain padded bits
                 no need to do any processing for these bytes */
              while(nDataBytes--)
              {
                pDst++;
              }

              /* clear only the padded bits starting from the end of valid data */
              for(x = ptPMSPage->nPageWidthPixels; x < (ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth);)
              {
                switch(x%8)
                {
                default:
                case 0:
                  *pDst = 0x00;
                  /* set padding to black... (for testing)
                  *pDst = 0xFF; */
                  x+=8;
                  pDst++;
                  break;
                case 1:
                  *pDst &= 0x80;
                  /* set padding to black... (for testing)
                  *pDst |= 0x7F; */
                  x+=7;
                  pDst++;
                  break;
                case 2:
                  *pDst &= 0xC0;
                  /* set padding to black... (for testing)
                  *pDst |= 0x3F; */
                  x+=6;
                  pDst++;
                  break;
                case 3:
                  *pDst &= 0xE0;
                  /* set padding to black... (for testing)
                  *pDst |= 0x1F; */
                  x+=5;
                  pDst++;
                  break;
                case 4:
                  *pDst &= 0xF0;
                  /* set padding to black... (for testing)
                  *pDst |= 0x0F; */
                  x+=4;
                  pDst++;
                  break;
                case 5:
                  *pDst &= 0xF8;
                  /* set padding to black... (for testing)
                  *pDst |= 0x07; */
                  x+=3;
                  pDst++;
                  break;
                case 6:
                  *pDst &= 0xFC;
                  /* set padding to black... (for testing)
                  *pDst |= 0x03; */
                  x+=2;
                  pDst++;
                  break;
                case 7:
                  *pDst &= 0xFE;
                  /* set padding to black... (for testing)
                  *pDst |= 0x01; */
                  x+=1;
                  pDst++;
                  break;
                }
              }
#ifdef PMS_LOWBYTEFIRST
              /* in little-endian case, we swapped the 32-bit word before clearing the padded bits above,
              We must now undo the swap */
              nSwapIterations = nBytesToSwap/4;
              while(nSwapIterations--)
              {
                *pTempBuffer = (*pTempBuffer << 24) | ((*pTempBuffer & 0xff00) << 8) | ((*pTempBuffer & 0xff0000) >> 8) | (*pTempBuffer >> 24);
                pTempBuffer++;
              }
#endif
              /* Advance to next line in the raster */
              pDst += (nBytesWithPadding - nBytesToSwap);
            }
          } /* ptPMSPage->uOutputDepth == 1 */
        } /* for number of planes */

        if ( ptPMSPage->uOutputDepth < 4 ) /* for 1bpp and 2bpp use RGB palette for reduced tiff sizes */
          nResult = GGE_TIFF(GGE_TIFF_INSEL_BAND_RGB_PALETTE, &tMyTIFFCompBand);
        else 
          nResult = GGE_TIFF(GGE_TIFF_INSEL_BAND_COMPOSITE, &tMyTIFFCompBand);
      }

      OSFree(tMyTIFFCompBand.paBandBuffer,PMS_MemoryPoolPMS);
      if(nResult != GGE_TIFF_SUCCESS)
      {
        PMS_SHOW_ERROR("\nTIFF_PageHandler: Failed to output tiff composite Band. Return value(%d)\n", nResult);
        return TIFF_Error_GGETiff;
      }
    } /*end of composite job handling */

    nResult = GGE_TIFF(GGE_TIFF_INSEL_CLOSE, NULL);
    if(nResult != GGE_TIFF_SUCCESS)
    {
        PMS_SHOW_ERROR("\nTIFF_PageHandler: Failed to close tiff. Return value(%d)\n", nResult);
        return TIFF_Error_GGETiff;
    }
#ifdef DIRECTVIEWPDFTIFF
    if((nCopyNo == 1) && (g_tSystemInfo.eOutputType == PMS_TIFF_VIEW) && (!g_bBackChannelPageOutput))
    {
      char pBuf[300];

      sprintf(pBuf,"\"start \"hello\" \"%s.tiff\"\"",tMyTIFFHeader.pszPathName);
      system(pBuf);
    }
#endif
    /* increment page number */
    nPageNo++;
  } /* end of for loop */

  return TIFF_NoError;
}


