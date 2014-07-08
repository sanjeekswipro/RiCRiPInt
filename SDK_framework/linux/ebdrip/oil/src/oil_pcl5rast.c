/* Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pcl5rast.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * PCL5 backend functions
 */

/**
 * \file
 * \ingroup OIL
 *
 * \brief A simple, partially optimized, implementation of PCL5 output.
 *
 * The incoming raster is mono halftone, pre-screened by the RIP, or RGB
 * 3x8-bit contone, in pixel-interleaved format.
 * There is no support for CMY or CMYK. 
 *
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "oil_pcl5rast.h"
#include "swcopyf.h"   /* for swncopyf() */
#include "skinkit.h"   /* for SkinMonitorf() */
#include "oil_pclcommon.h"    /* For paper sizes */
#include "hqnstrutils.h"  /* for strlen_int32 */

#include "oil_malloc.h"

/*#define CAPTUREPCL*/
/**
 * \brief Output compression mode.
 */
static const uint32 fCompressMode = eAdaptiveCompression;
/* static const uint32 fCompressMode =  eDeltaRowCompression; */
/*
 * \brief PCL5 Raster description.
 *
 * Note on margin handling.
 * In this example, we'll assume that the incoming raster is exactly the same
 * size as the target media, and that the printer has 1/6 inch of
 * unprintable area at each edge, and discard that much image from all sides.
 * For the left and right margins, we'll round up to the nearest 8 pixels,
 * so that we can work with whole bytes without shifting the raster by 1..7
 * pixels.
 */

typedef struct _PCL5_RASTER
{
  eRFLIST   nRF;                  /* Enumerates raster type */
  uint8   * pDRC;                 /* Will hold 1 line of alternatively compressed raster. */
  uint8   * pBandBuf;             /* Holds a band of compressed data, for greater efficiency */
  uint8   * pData;                /* Pointer into pBandBuf */
  uint8   * pSeedRow;             /* Initially, a line of '0' bytes, also carries row into next band */
  uint32    cbBandBuf;            /* Size of the compressed band buffer */
  int32     cbLineWidthInBytes;   /* Uncompressed, clipped, output line length, in bytes */
  int32     cbPGBLine;            /* Length of each line within the incoming band */
  int32     nLastLineToSend;      /* The bottom margin's location */
  int32     nNonPrintMarginPix;
  int32     nNonPrintMarginBytes;
  uint32    nCmdDigits;           /* Used for reserving space for, and setting the PCL commands */
  uint32    nCmdReserve;          /* Used for reserving space for the PCL commands */
  uint8   * pCmd;                 /* Temporary buffer for the command */

  MEDIASIZEENTRY * pMSE;          /* Lookup table entry */
} PCL5_RASTER;

/**
 * \brief The file name format string when outputting to a file.
 * ".pcl" is appended to the fully-qualified path.
 */
uint8 *fileformatPCL5 = (uint8*)"%f.pcl";
#ifdef CAPTUREPCL
FILE *pPrintFile;
#endif

static PCL5_RASTER_handle * pcl5NewRasterHandle (uint8 * ps_filename);
static int32   pcl5EndPage (PCL5_RASTER_handle *handle);
static int32   pcl5NewHeader (PCL5_RASTER_handle *handle, RasterDescription * pRD);
static __inline uint32 findRGBLineEnd (uint8 *pData, int32 nLen);
static __inline uint32 findMonoLineEnd (uint8 *pData, int32 nLen);

static   int32  bFirstPage = 1;

/**
 * \brief Standard clRip RASTER_START callback function to handle
 * the start of a page.
 *
 * If the job has multiple pages, we should already have an opened raster
 * handle. Otherwise we create a raster handle suitable for clRip or PrintPipeline.
 *
 * \param ps_filename A formatted PostScript pathlist/filename, based upon the input
 * job name and the \c fileformatPCL5 string supplied in this file.
 *
 * \param pRD [in] Describes the properties of the current page's raster image.
 *
 * \param handlep [in/out] Returns the raster handle created when starting the first
 * page of a new job, or passes in the already created handle for subsequent pages.
 *
 * \return RASTER_noErr on success, or else one of the other RASTER_xxxx
 * values defined in rasthand.h
 *
 */
int32 PCL5_RASTER_start(uint8 *ps_filename, RasterDescription *pRD, RASTER_handle *handlep)
{
  PCL5_RASTER_handle * handle = NULL;
  PCL5_RASTER * phPcl = NULL;
  int32         result = RASTER_noErr;
  int32         cbHdr;            /* Byte count of the PCL/PJL header data. */
  eRFLIST       nRF;              /* Incoming raster style. */
  int32         cbCompBuff = 0;   /* Size of pCompressBuffs */
  int32         nPixelsPerByte;   /* For convenience when managing raster pointers. */
  int           n;
  uint32        bLandscape = FALSE; /* Not used in this example, but could be
                             applied to @PJL SET ORIENTATION and/or Esc*r#F  */
#ifdef WIN32							 
  DOC_INFO_1 DocInfo;
  char PrinterName[128] ;
  DWORD cbPrinterName = 128;
  char DataType[] = "RAW";
  DWORD cWritten;
  BOOL bresult;
#endif 
/* Check job's raster format. */
  if ((nRF = getRasterFormat (pRD)) == RF_UNKNOWN)
    return RASTER_requestErr;

  if (*handlep == (RASTER_handle) NULL)
  {
    bFirstPage = 1;
    if ( (handle = pcl5NewRasterHandle(ps_filename)) == NULL )
      return RASTER_requestErr;
#ifdef WIN32	  
    bresult = GetDefaultPrinter(PrinterName, &cbPrinterName);
    if(bresult == 0)
    {
      result = RASTER_requestErr;
      goto PCL5_raster_start_error;
    }
    bresult = OpenPrinter(PrinterName, &(handle->fd_out), NULL);
    if(bresult == 0)
    {
      result = RASTER_requestErr;
      goto PCL5_raster_start_error;
    }
    DocInfo.pDocName = (LPSTR)ps_filename;
    DocInfo.pOutputFile = NULL;
    DocInfo.pDatatype = DataType;
    result = StartDocPrinter(handle->fd_out, 1, (LPBYTE)(&DocInfo));
    if ( result == 0 ) {
      result = RASTER_requestErr;
      goto PCL5_raster_start_error;
    }
    else
    {
      result = RASTER_noErr;
    }
#else	
    handle->fd_out = fopen(handle->filename,"wb");
    if(!(handle->fd_out))
    {
      result = RASTER_requestErr;
      goto PCL5_raster_start_error;
    }
#endif
#ifdef CAPTUREPCL
    pPrintFile = fopen("c:\\ebdwrapperout.prn", "wb");
#endif
  }
  else
  {
    bFirstPage = 0;
    handle = (PCL5_RASTER_handle *) * handlep;
  }

  phPcl = (PCL5_RASTER *) handle->pPrivate;

  /* Clean start for first page */
  if (bFirstPage)
    memset (phPcl, 0, sizeof(PCL5_RASTER));

  phPcl->nRF = nRF;
  nPixelsPerByte = 8 / pRD->rasterDepth;

  /* Calculate the size of one line of compression buffer.
   * We'll be compressing one line at a time, either 24bpp RGB, or 1bpp mono.
   * The buffer needs to accommodate one line of worst-case compressed
   * raster.
   */
  cbCompBuff = pRD->numChannels * (pRD->imageWidth + 7) / nPixelsPerByte;
  cbCompBuff += ((cbCompBuff + 7) / 8);  /* Worst-case Ctl bytes allowance for delta-row */

  /* Calculate the maximum compressed line numeric string length */
  n = cbCompBuff;
  phPcl->nCmdDigits = 0;
  do  {
    ++phPcl->nCmdDigits;
  } while ((n /= 10) > 0);

  /*
   * To reserve space for the PCL command, we need to precalculate the string
   * lengths for (eg)
   * Esc * b 2 m 00000 W
   * When we have the compressed line's length, we can precede it with the PCL,
   * using this value to format the %0*d (padding it to the predicted length).
   * This avoids having to use a separate buffer for compressing the lines, and
   * then writing a variable-length command, and appending the compressed data.
   */
  phPcl->nCmdReserve = 6 + phPcl->nCmdDigits;
  phPcl->pCmd = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, phPcl->nCmdReserve+1);
 if ( phPcl->pCmd == NULL ) {
    result = RASTER_requestErr;
    goto PCL5_raster_start_error;
  }

  /* Unprintable margins in PIXELS.
   * Assume 1/6 inch, which is typical.
   * Assume XRes == YREs, this appears to be safe in this context.
   */
  phPcl->nNonPrintMarginPix = (int)(pRD->xResolution + 5.01) / 6;
  /* If the RIP has specified larger clipping margins, then use them: */
  if (phPcl->nNonPrintMarginPix < (0 - pRD->leftMargin))
    phPcl->nNonPrintMarginPix   =  0 - pRD->leftMargin;
  if (phPcl->nNonPrintMarginPix < (0 - pRD->topMargin))
    phPcl->nNonPrintMarginPix   =  0 - pRD->topMargin;
  phPcl->nNonPrintMarginBytes = pRD->numChannels * ((phPcl->nNonPrintMarginPix + 4) / nPixelsPerByte);

  phPcl->nLastLineToSend = pRD->imageHeight - phPcl->nNonPrintMarginPix;  /* input line index */

  phPcl->pMSE = pclPerformMediaLookup (pRD, &bLandscape);

  /* Create a compression buffer for a band of raster: */
  phPcl->cbBandBuf = (pRD->bandHeight + 1) * (cbCompBuff + 1024);
  phPcl->pBandBuf = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, phPcl->cbBandBuf);
  if (NULL == phPcl->pBandBuf) {
    result = RASTER_requestErr;  /* There isn't an out-of-memory error defined */
    goto PCL5_raster_start_error;
  }

  /* Send the PJL/PCL preamble to the device */
  cbHdr = pcl5NewHeader(handle, pRD);
  if (cbHdr <= 0) {
    result = RASTER_requestErr;
    goto PCL5_raster_start_error;
  }

#ifdef WIN32
  bresult = WritePrinter(handle->fd_out, phPcl->pBandBuf, cbHdr, &cWritten);
  if(bresult == 0)
  {
    result = RASTER_requestErr;
    goto PCL5_raster_start_error;
  }
#else  
  fwrite(phPcl->pBandBuf, 1, cbHdr, handle->fd_out);
#endif
#ifdef CAPTUREPCL
  fwrite(phPcl->pBandBuf, 1, cbHdr, pPrintFile);
#endif
  /* Create compression buffers for a single colourant: */
  phPcl->pDRC = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, cbCompBuff + 1024);
  if (NULL == phPcl->pDRC) {
    result = RASTER_requestErr;
    goto PCL5_raster_start_error;
  }

  /* Width values are in pixels, calculate the buffer requirement in bytes. */
  phPcl->cbLineWidthInBytes = pRD->imageWidth - (2 * phPcl->nNonPrintMarginPix);
  phPcl->cbLineWidthInBytes = pRD->numChannels * (phPcl->cbLineWidthInBytes + nPixelsPerByte - 1) / nPixelsPerByte;
  phPcl->cbPGBLine = pRD->dataWidth / 8;    /* bits -> bytes */

PCL5_raster_start_error:
  if ( result != RASTER_noErr ) {
    /* Error happened - clean up */
    (void) PCL5_RASTER_job_end((RASTER_handle)&handle);
  }

  *(handlep) = (RASTER_handle *) handle;

  return result;
}


/**
 * \brief Multiple calls are made to this function for each page.
 * The OEM might apply late screening here, and wrap the raster
 * data in printer commands, or they might pass the raw image data
 * to one or more filters in the PrintPipeline for further processing.
 * It is now possible for the OEM to reliably set the number of raster
 * lines delivered to this function, see /BandLines in the documentation.
 * The last band may contain less lines than the others.
 *
 * \param rHandle This is the handlep value we assigned in the
 * PCL5_RASTER_start function.
 *
 * \param pBand Points at the first byte of the current band of raster
 * image data.
 *
 * \param topline The 0-based index of the first raster image line in
 * the current band.
 *
 * \param nLinesThisTime The number of lines of image data in the current
 * band. The line count in the last band of a page may be different
 * from all of the preceding ones.
 *
 * \param nLineWidth This is the length, in bytes, of each line of
 * image within the band, NOT including any padding. The actual imageable
 * portion of the line was derived earlier from
 * pRasterDescription->imageWidth.
 *
 * \param channel We ignore this value, as we're using RGB data in
 * pixel-interleaved format, which is considered as a single channel.
 *
 * \return RASTER_noErr on success, or else one of the other RASTER_xxxx
 * values defined in rasthand.h
 *
 */

int32 PCL5_RASTER_write_data(RASTER_handle rHandle, void  *pBand, int32 topline,
                int32 nLinesThisTime, int32 nLineWidth, int32 channel)
{
  PCL5_RASTER_handle * handle = (PCL5_RASTER_handle*) rHandle;
  PCL5_RASTER * phPcl = (PCL5_RASTER *) handle->pPrivate;
  uint32        cbSend, result;
  uint8       * pRasterLine = NULL;
  uint8       * pOut = NULL;
  uint8       * pSeed;
  int32         nLine;
  int32         fDoMemCpy;
  static const char  szAllZeros[]     = "\033*b0m0W";
  static const char  szUncompressed[] = "\033*b0m%0*dW";
  static const char  szPackBits[]     = "\033*b2m%0*dW";
  static const char  szDeltaRow[]     = "\033*b3m%0*dW";
  char        * pszMethod;    /* One of the above */
#ifdef WIN32
  DWORD cWritten;
#else  
  float cWritten;
#endif
  UNUSED_PARAM(int32, nLineWidth);
  UNUSED_PARAM(int32, channel);
  assert (phPcl->pBandBuf);

  if (0 == topline) {
    /* We cannot assume that all of the pages in a job have the same dimensions,
     * so replace the seed row buffer at the start of each page : it would need
     * to be ZERO'd anyway.
     */
    if (phPcl->pSeedRow) {
      OIL_free (OILMemoryPoolJob, phPcl->pSeedRow);
      phPcl->pSeedRow = NULL;
    }
    if (fCompressMode >= eDeltaRowCompression) {
      /* We must have a zero'd seed row at the start of the page, and
       * must keep a local copy of the last band line for seeding the
       * first line of the next band.
       */
      phPcl->pSeedRow = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, phPcl->cbLineWidthInBytes+4);
      /* PP added 16-7-2009 as the seed is not initialized; not sure if this is correct for all cases!!!!!!*/
      memset(phPcl->pSeedRow,'\0',phPcl->cbLineWidthInBytes+4);
  /* ..and we should free and NULL it at the end of the job. */
    }
  }
  pSeed = phPcl->pSeedRow;
  pRasterLine = (uint8 *)pBand + phPcl->nNonPrintMarginBytes;
  phPcl->pData = phPcl->pBandBuf;

  for (nLine = 0; nLine < nLinesThisTime; nLine++) {
    /* Skip any unprintable top/bottom margin areas */
    if ((phPcl->nNonPrintMarginPix < topline) && (phPcl->nLastLineToSend > topline)) {
      uint32 nLastNonWhite = 0;

      /* Some compression types allow us to ignore trailing white space. */
      switch (fCompressMode)
      {
      case eNoCompression:
      case eRLECompression:
      case eAdaptiveCompression:
        if (phPcl->nRF == RF_RGB_CT)
          nLastNonWhite = findRGBLineEnd (pRasterLine, phPcl->cbLineWidthInBytes);
        else
          nLastNonWhite = findMonoLineEnd (pRasterLine, phPcl->cbLineWidthInBytes);

        if (nLastNonWhite == 0)
        {
          /* Send a zero line length command, and no data. */
          memcpy (phPcl->pData, (uint8 *)szAllZeros, 7);
          phPcl->pData += 7;
        }
        break;

      default:
        /* We must store the entire line. */
        nLastNonWhite = phPcl->cbLineWidthInBytes + 1;
        break;
      }

      if (nLastNonWhite > 0)
      {
        int32 cbCmd; /* Number of command bytes written. */
        int32 cbRLE, cbDRC;

        switch (fCompressMode)
        {
        case eNoCompression:
          cbCmd = swncopyf(phPcl->pCmd, (int32)phPcl->nCmdReserve+1, (uint8 *)szUncompressed, phPcl->nCmdDigits, nLastNonWhite);
          memcpy (phPcl->pData, (uint8 *)phPcl->pCmd, cbCmd);  /* drop the trailing 0 */
          phPcl->pData += cbCmd;
          memcpy (phPcl->pData, pRasterLine, nLastNonWhite);
          phPcl->pData += nLastNonWhite;
          break;

        case eDeltaRowCompression:
          cbDRC = pclCompressDeltaRow(phPcl->pData+phPcl->nCmdReserve, pSeed, pRasterLine, phPcl->cbLineWidthInBytes, FALSE);
          cbCmd = swncopyf(phPcl->pCmd, (int32)phPcl->nCmdReserve+1, (uint8 *)szDeltaRow, phPcl->nCmdDigits, cbDRC);
          memcpy (phPcl->pData, (uint8 *)phPcl->pCmd, cbCmd );  /* drop the trailing 0 */
          phPcl->pData += cbCmd;

          phPcl->pData += cbDRC;
          break;

        case eRLECompression:
          cbRLE = pclCompressPackbits(phPcl->pData+phPcl->nCmdReserve, pRasterLine, nLastNonWhite);
          cbCmd = swncopyf(phPcl->pCmd, (int32)phPcl->nCmdReserve+1, (uint8 *)szPackBits, phPcl->nCmdDigits, cbRLE);
          memcpy (phPcl->pData, (uint8 *)phPcl->pCmd, cbCmd );  /* drop the trailing 0 */
          phPcl->pData += cbCmd;
          phPcl->pData += cbRLE;
          break;

        case eAdaptiveCompression:
          /* Try both compression methods, and use the better one of the 2, IF it's better than uncompressed */
          cbDRC = pclCompressDeltaRow(phPcl->pDRC, pSeed, pRasterLine, phPcl->cbLineWidthInBytes, FALSE);
          cbRLE = pclCompressPackbits(phPcl->pData+phPcl->nCmdReserve, pRasterLine, nLastNonWhite);
          if (cbRLE < cbDRC) {
            cbSend = cbRLE;
            pszMethod = (char *)szPackBits;
            fDoMemCpy = FALSE;
          }
          else {
            cbSend = cbDRC;
            pOut = phPcl->pDRC;
            pszMethod = (char *)szDeltaRow;
            fDoMemCpy = TRUE;
          }
          /* Adaptive allows us to send the (clipped) uncompressed data, if compression fails */
          if (cbSend > nLastNonWhite) {
            cbSend = nLastNonWhite;
            pOut = pRasterLine;
            pszMethod = (char *)szUncompressed;
            fDoMemCpy = TRUE;
          }
          cbCmd = swncopyf(phPcl->pCmd, (int32)phPcl->nCmdReserve+1, (uint8 *)pszMethod, phPcl->nCmdDigits, cbSend);
          memcpy (phPcl->pData, (uint8 *)phPcl->pCmd, cbCmd);  /* drop the trailing 0 */
          phPcl->pData += cbCmd;
          if (fDoMemCpy)
            memcpy (phPcl->pData, pOut, cbSend);
          phPcl->pData += cbSend;
          break;

        default:
          assert(FALSE);
        }
      }
      pSeed = pRasterLine;  /* Must not change while we're discarding the top margin. */
    }
    pRasterLine += phPcl->cbPGBLine;
    ++topline;
  }
  if (phPcl->pSeedRow) {
    assert (pRasterLine);
    memcpy(phPcl->pSeedRow, pSeed, phPcl->cbLineWidthInBytes);
  }
  cbSend = (uint32)(phPcl->pData - phPcl->pBandBuf);
#ifdef WIN32
  result = WritePrinter(handle->fd_out, phPcl->pBandBuf, cbSend, &cWritten);
  if(result == 0)
    return RASTER_fileOutputErr;
#else
  fwrite(phPcl->pBandBuf, 1, cbSend, handle->fd_out);
#endif
#ifdef CAPTUREPCL
  fwrite(phPcl->pBandBuf, 1, cbSend, pPrintFile);
#endif
  return RASTER_noErr;
}

/**
 * \brief This function is called once at the end of each page in each job.
 * Suitable PJL and/or PCL for endpage will be created and sent to the printer.
 *
 * \param handlep This is the handlep value we assigned in our
 * PCL5_RASTER_start function.
 *
 * \param result This should be whatever value was returned by the
 * last call to PCL5_RASTER_write_data.
 *
 * \return RASTER_noErr on success, or else one of the other RASTER_xxxx
 * values defined in rasthand.h
 */
int32 PCL5_RASTER_finish(RASTER_handle *handlep, int32 result)
{
  PCL5_RASTER_handle *handle = (PCL5_RASTER_handle*) *handlep;
  PCL5_RASTER * phPcl;

  assert(handle);
  if (pcl5EndPage(handle) != RASTER_noErr)
    result = RASTER_fileOutputErr;

  phPcl = (PCL5_RASTER *) handle->pPrivate;

  /* We don't assume that all pages in the job will be the same size/orientation.
   * So we free the buffer[s] and recreate for new page.
   */
  if (phPcl->pDRC) {
    OIL_free (OILMemoryPoolJob, phPcl->pDRC);
    phPcl->pDRC = NULL;
  }
  if (phPcl->pBandBuf) {
    OIL_free (OILMemoryPoolJob, phPcl->pBandBuf);
    phPcl->pBandBuf = NULL;
  }
  if (phPcl->pCmd) {
    OIL_free (OILMemoryPoolJob, phPcl->pCmd);
    phPcl->pCmd = NULL;
  }
  return result;
}

/**
 * \brief This function is called after the last page has been closed.
 *
 * \param handlep This is the handlep value we assigned in our
 * PCL5_RASTER_start function.
 *
 * \return RASTER_noErr on success, or else one of the other RASTER_xxxx
 * values defined in rasthand.h
 */
int32 PCL5_RASTER_job_end(RASTER_handle *handlep)
{
  char pEndPJL[32];

  int32 result;
#ifdef WIN32
  DWORD cWritten;
#endif 
  PCL5_RASTER_handle *handle = (PCL5_RASTER_handle*) *handlep;

  if (handle) {
    PCL5_RASTER * phPcl = (PCL5_RASTER *) handle->pPrivate;
    if (phPcl && phPcl->pSeedRow) {
      OIL_free (OILMemoryPoolJob, phPcl->pSeedRow);
      phPcl->pSeedRow = NULL;
    }
    if (handle->fd_out >= 0 ) {
      sprintf (pEndPJL, "%c%c-12345X@PJL EOJ \015\012", 0x1B, 0x25);
#ifdef CAPTUREPCL
      fwrite(pEndPJL, 1, strlen(pEndPJL), pPrintFile);
      fclose(pPrintFile);
#endif
#ifdef WIN32
      result = WritePrinter(handle->fd_out, (uint8 *)pEndPJL, (DWORD) strlen(pEndPJL), &cWritten);
#else
      result = fwrite(pEndPJL, 1, strlen(pEndPJL), handle->fd_out);
#endif
      if(result == 0)
      {
          return RASTER_fileOutputErr;
      }
#ifdef WIN32
      EndDocPrinter(handle->fd_out);
      ClosePrinter(handle->fd_out);
      handle->fd_out = (HANDLE)-1;
#else
      fclose(handle->fd_out);
      system("lpr -r printready.prn");
#endif	
    }
    OIL_free (OILMemoryPoolJob, handle);
    *handlep = (RASTER_handle*)NULL;
  }

  return RASTER_noErr;
}

/**
 * \brief We can reduce the amount of data we transfer by discarding blank
 * pixels at the ends of lines of RGB contone (pixel-interleaved ONLY) and
 * monochrome halftone rasters.
 *
 * Remember that the image start point is offset by the clipped left margin value.
 * This function could be optimized to test integer-sized data, if we used loop-
 * unrolling, and handled the alignment of the start and end addresses.
 *
 * \param  pData Points at the first byte of the buffer to be checked.
 * \param  nLen Byte count of the data to be checked.
 * \return the byte offset of the last non-white byte:
 *         That's all the raster we need to send in RLE/Packbits mode.
 */

static __inline uint32 findMonoLineEnd (uint8 *pData, int32 nLen)
{
  uint8 * pEnd = pData + nLen;
  while (pData <= --pEnd) {
    if (*(pEnd)) {
      return 1 + (uint32)(pEnd - pData);
    }
  }
  return 0;
}

/* We're looking for '0' PIXELS.
 * In an RGB image we round the length up to the next pixel's boundary.
 */
static __inline uint32 findRGBLineEnd (uint8 *pData, int32 nLen) {
  uint32  nEnd;
  uint8 * pEnd = pData + nLen;
  while (pData <= --pEnd) {
    if (*(pEnd)) {
      nEnd = (uint32)(pEnd - pData) + 2;
      nEnd = (nEnd / 3) * 3;
      return (1+nEnd);
    }
  }
  return 0;
}

/**
 * \brief Creates a new pcl5 raster handle.
 *
 * \param ps_filename the PS name including the output PS device.
 * \return the new handle if successful; NULL otherwise.
 */
static PCL5_RASTER_handle * pcl5NewRasterHandle (uint8 * ps_filename)
{
  PCL5_RASTER_handle * handle = NULL;
  UNUSED_PARAM(uint8 *, ps_filename);
#ifdef WIN32
  handle = (PCL5_RASTER_handle*) OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock,
                                          sizeof(PCL5_RASTER_handle) +
                                          sizeof(PCL5_RASTER));
  if(handle!=NULL)
  {
    handle->fd_out = (HANDLE)-1;
    handle->pPrivate = (uint8 *)(handle + 1);
  }
#else
  handle = (PCL5_RASTER_handle *) OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock,
                                            sizeof(PCL5_RASTER_handle));
  strcpy((char *)handle->filename,"printready.prn");
#endif
  return handle;
}

/**
 * \brief Constructs a PCL/PJL header block for the page.
 *
 * NB: Most printers will ignore any PJL commands which they don't understand.
 * The ones used here are typical. Specific printers may require additional
 * commands, or different command parameters.
 * Some printers require the Configure Raster Data command, not the Simple Colour one.
 * That mode locks-out  ESC*r\#u, Simple Color and ESC*v\#W[ data], and
 * Configure Image Data. It is not used in this simple example.
 *
 * Some parameters are obtained from the PrintTicket.
 *
 * \param handle PCL5_Raster_handle.
 * \param pRD RasterDescription struct supplied by the RIP.
 * \return 0 on failure, otherwise the header length in bytes.
 */
static int32 pcl5NewHeader (PCL5_RASTER_handle *handle, RasterDescription * pRD)
{
  PCL5_RASTER * phPcl = (PCL5_RASTER *) handle->pPrivate;
  char * pHdr = (char*)phPcl->pBandBuf;
#ifdef WIN32
  BOOL bresult;
#endif 
  if ((NULL == pRD) || (NULL == phPcl)) {
    /* Fatal error, but fail gracefully. */
    return 0;
  }

  /* Reset unit */
  memset (pHdr, 0, phPcl->cbBandBuf);
#ifdef WIN32  
  bresult = StartPagePrinter(handle->fd_out);
  if(bresult == 0)
    return 0;
#endif 
  if(bFirstPage == 1)
  {
    pHdr = writeJobPJL (pHdr, pRD, phPcl->pMSE, 5);

  /* Switch to PCL mode with current pen position as CAP */
    sprintf (pHdr, "%cE", 0x1B);       pHdr += strlen (pHdr);
  }
  sprintf (pHdr, "%c%c1A", 0x1B, 0x25); pHdr += strlen (pHdr);

  /* Turn off Source and Pattern Transparency */
  sprintf (pHdr, "%c*v1N", 0x1B);       pHdr += strlen (pHdr);
  sprintf (pHdr, "%c*v1O", 0x1B);       pHdr += strlen (pHdr);
  /* Specify no negative motion allowed */
  sprintf (pHdr, "%c&a1N", 0x1B);       pHdr += strlen (pHdr);

  /* Move H and V CAP by 0 RTL units: */
  sprintf (pHdr, "%c*p0X", 0x1B);       pHdr += strlen (pHdr);
  sprintf (pHdr, "%c*p0Y", 0x1B);       pHdr += strlen (pHdr);

  /* Source raster width/height, in pixels. We're only using 1 value for all 4 margins in this example. */
  sprintf (pHdr, "%c*r%dS", 0x1B, pRD->imageWidth - (2 * phPcl->nNonPrintMarginPix));  /* Width */
  pHdr += strlen(pHdr);
  sprintf (pHdr, "%c*r%dT", 0x1B, pRD->imageHeight - (2 * phPcl->nNonPrintMarginPix));  /* Height */
  pHdr += strlen(pHdr);

  /* Raster Graphics Resolution commands */
  sprintf (pHdr, "%c*t%dR", 0x1B, (int)pRD->xResolution); pHdr += strlen(pHdr);
  /* sprintf (pHdr, "%c&u%dD", 0x1B, (int)pRD->yResolution); pHdr += strlen(pHdr); */

  /* Raster Graphics Presentation command,
   * 0 : Raster image prints in orientation of logical page
   * 3 : Raster image prints along the width of the physical page
   */
  sprintf (pHdr, "%c*r0F", 0x1B);  pHdr += strlen(pHdr);

  /* Device mode
  sprintf (pHdr, "%c*o3W%c%c%c", 0x1B, 0x06, 0x04, 0x00); pHdr += 8;
  */

  /* Select halftoning. 0 and 3 are most likely to work on an arbitrary printer. */
  sprintf (pHdr, "%c*t3J", 0x1B);
  pHdr += strlen(pHdr);


  /* Esc*r#U can create a 2-pen Black-and-White palette,
   * an 8-pen RGB palette, or an 8-pen CMY palette. When using the
   * Simple Color mode, the pixel encoding mode is always indexed
   * planar.
   * ESC*r#U Color selection from a CMY or KCMY palette.
   * # = –3 - 3 planes, device CMY palette
   *      1 - Single plane K (Black) palette
   *      3 - 3 planes, device RGB palette
   *
   * For CMYK halftone rasters, we could combine pixels to create colour indexes in either RGB or CMY.
   * RGB  CMY
   * 0    7    Black
   * 1    6    Red
   * 2    5    Green
   * 3    4    Yellow
   * 4    3    Blue
   * 5    2    Magenta
   * 6    1    Cyan
   * 7    0    White
   */

  /* Configure Image Data command (Esc*v#W), allows a maximum of 24 bits per pixel.
   * # is the ascii numeric value of the number of binary command bytes following the W.
   * Since the RIP doesn't readily support CMY, we'll use RGB in this example.
   * The short and long forms of the CID command use a common 6-byte
   * header, regardless of which color space is specified:
   * [0]  Color space, one of:
   *      0 Device RGB (default) 1931 standard, D6500
   *      1 Device CMY
   *      2 Colorimetric RGB Spaces
   *      3 CIE L*a*b*, CIE 1976 Uniform Color Space based on 1931 standard, D6500
   *      4 Luminance-Chrominance Spaces
   * [1]  Pixel encoding mode, one of :
   *      0 Indexed by Plane, Bits/index must be 1, 2, 3, 4, 5, 6, 7, or 8
   *      1 Indexed by Pixel Bits/index must be 1, 2, 4, or 8
   *      2 Direct by Plane 1 bit per primary (RGB or CMY only)
   *      3 Direct by Pixel 8 bits per primary (All Color Spaces)
   *   NB: Direct pixel encoding modes (2, 3) cannot be compressed using
   *       raster compression modes 1 or 2.
   *       Planar pixel encoding modes (0, 2) cannot be compressed using
   *       raster compression mode 5.
   *
   *    MODE 0:
   *       In mode 0 successive planes of data are sent for each raster row.
   *       Esc*v6W 00 00 03 08 08 08
   *       Sets color space to RGB, pixel encoding mode to 0, palette size to 8
   *       (3 planes), last 3 bytes are ignored.
   *       A plane contains one bit for  each pixel in a row. A pixel is not fully
   *       defined until it has received all the planes for that row. The planes in a
   *       row form index numbers that define a pixel by selecting a palette
   *       entry. Assuming 3 bits per index, the underlined column of bits below
   *       is the palette index for pixel 3 of row 1 (i1 is lsb; i3 is msb). Note that
   *       the Transfer Raster Data by Plane command (Esc*b#V) is used for all
   *       planes except the last plane of each row, which uses the Transfer
   *       Raster Data by Row command (Esc*b#W).
   *         Esc*b#V row 1 plane 1 i1 i1 i1 i1 i1 i1 i1 i1 (1 byte)
   *         Esc*b#V plane 2       i2 i2 i2 i2 i2 i2 i2 i2
   *         Esc*b#W plane 3       i3 i3 i3 i3 i3 i3 i3 i3
   *
   *    MODE 1:
   *       In mode 1, each pixel in a row is fully specified before any bits are
   *       sent for the next pixel.
   *       Esc*v6W 00 01 04 04 04 04
   *       Sets color space to RGB, pixel encoding mode to 1, palette size to 16
   *       (4 bits to address palette index). Last 3 bytes are ignored.
   *       The bits for each pixel form a palette index  *       number. Assuming 4 bits per index, the underlined block below is the
   *       palette index for pixel 2 of row 1 (i1 is lsb).
   *          Esc*b#W row 1    i4 i3 i2 i1 i4 i3 i2 i1 (1 byte)
   *          Esc*b#W row 2    i4 i3 i2 i1 i4 i3 i2 i1 . . .
   *          Esc*b#W row 3    i4 i3 i2 i1 i4 i3 i2 i1 . . .
   *
   *    MODE 2 :
   *       In mode 2, the color raster data for each row is sent as sequential planes,
   *       but the pixel color is directly specified, rather than indexing into a palette.
   *       Esc*v6W 00 02 01 01 01 01
   *       Sets color space to RGB, pixel encoding mode to 2. Palette size is ignored
   *       as the colours aren't indexed. Last 3 bytes are always 1 for this mode.
   *          Esc*b#V row 1 red plane    r r r r r r r r (1 byte)
   *          Esc*b#V green plane        g g g g g g g g .....
   *          Esc*b#W blue plane        b b b b b b b b .....
   *          Esc*b#V row 2 red plane    r r r r r r r r .........
   *
   *    MODE 3 :
   *       In mode 3, the color raster data is downloaded pixel by pixel (as in
   *       mode 1), but each pixel directly specifies each color component (as in
   *       mode 2). Assuming Device RGB space with 8 bits per primary:
   *       Esc*v6W 00 03 00 08 08 08
   *       Sets color space to RGB, pixel encoding mode to 3. Palette size is
   *       ignored. Send 3 x 8 bit colorants per pixel.
   *          Esc*b#W row 1    r7–r0 g7–g0 b7–b0 (3 bytes, 1 x 24 bpp pixel). . .
   *          Esc*b#W row 2    r7–r0 g7–g0 b7–b0 . . .
   *          Esc*b#W row 3    r7–r0 g7–g0 b7–b0 . . .
   *
   * [2]  Bits/index
   *      This value specifies the number of bits required to access all palette entries.
   *      In pixel encoding modes 2 and 3 (direct), this value determines palette size,
   *      but has no effect on the specification of raster data.
   * [3, 4, 5]
   * Bits/primary #1
   * Bits/primary #2
   * Bits/primary #3
   *      Ignored in pixel encoding modes 0 and 1, but affects the black and white
   *      references in device-dependent color spaces. In Device RGB, the black
   *      reference for primary #1 is set to 0 and the white reference is set to
   *      2n – 1, where n is the number of bits for primary #1.
   *      These references are reversed in Device CMY color space.
   *      In pixel encoding mode 3, these bytes designate the number of data bits
   *      needed to specify the CMY or RGB colour for each pixel.
   */
  switch (phPcl->nRF)
  {
  case RF_MONO_HT:
    sprintf (pHdr, "%c*r1U", 0x1B);
    pHdr += strlen(pHdr);
    break;
  case RF_MONO_CT:
    {
      int i;

      /* 8 bits per pixel, and using a color palette. */
      sprintf (pHdr, "%c*v6W%c%c%c%c%c%c", 0x1B, 0, 1, 8, 0, 0, 0); /* 8 bit indexes */
      pHdr += 11;

      /*
       * Create a grayscale palette.
       * Note we create the palette such that we don't have to invert the raster pixels.
       */
      for (i = 0; i < 256; i ++)
      {
        /* Select palette index. */
        sprintf (pHdr, "%c*v%dI", 0x1B, 255 - i); pHdr += strlen(pHdr);

        /* Set RGB values. */
        sprintf (pHdr, "%c*v%dA", 0x1B, i); pHdr += strlen(pHdr);
        sprintf (pHdr, "%c*v%dB", 0x1B, i); pHdr += strlen(pHdr);
        sprintf (pHdr, "%c*v%dC", 0x1B, i); pHdr += strlen(pHdr);
      }
    }
    break;
  default:
    /* RGB, 8 bits per pixel. */
    sprintf (pHdr, "%c*v6W%c%c%c%c%c%c", 0x1B, 0, 3, 8, 8, 8, 8);
    pHdr += 11;
    break;
  }

  /* Start Raster Graphics command.
   * 0 Start raster graphics at logical page left bound.
   * 1 Start raster graphics at the current active position (CAP).
   * 2 Turn on scale mode and start graphics at logical page left boundary.
   * 3 Turn on scale mode and start graphics at the CAP.
   */
  sprintf (pHdr, "%c*r1A", 0x1B);
  pHdr += strlen(pHdr);

  return (int)((uint8*)pHdr - phPcl->pBandBuf);
}

/**
 * \brief This function is called once at the end of each page in each job.
 * \param handle the pcl5 raster handle.
 * \return RASTER_noErr if no error; error code otherwise.
 */
static int32 pcl5EndPage (PCL5_RASTER_handle *handle)
{
  int32  n;
  char   abBuff[512];
  char * pHeader = &abBuff[0];
  int32  result = RASTER_noErr;
#ifdef WIN32
  DWORD cWritten;
  BOOL bresult;
#endif 

  sprintf (pHeader, "\033*rC\x0C");  /* stop raster graphics and FF */
  pHeader += strlen(pHeader);
  n = strlen_int32 (abBuff);

#ifdef WIN32
  result = WritePrinter(handle->fd_out, (uint8 *)abBuff, n, &cWritten);
#else
  result = fwrite(abBuff, 1, n, handle->fd_out);
#endif
#ifdef CAPTUREPCL
  fwrite(abBuff, 1, n, pPrintFile);
#endif
  if(result == 0)
  {
    return RASTER_fileOutputErr;
  }
  else
  {
#ifdef WIN32  
    bresult = EndPagePrinter(handle->fd_out);
    if(bresult == 0)
      return RASTER_fileOutputErr;
    else
#endif
      return RASTER_noErr;
  }
}


