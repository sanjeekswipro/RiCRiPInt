/* Copyright (C) 2005-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_pdf_out.c(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup PMS
 *  \brief PDF Output Method.
 *
 * These routines output the raster data in the Portable Document Format (PDF).
 *
 * The structure of the ebdwrapper PDF output file is simply an image wrapped in some PDF commands.
 *
 * This module supports;
 * \li 1, 2, 4, 8 and 16 bits per pixel (per colorant).
 * \li CMYK, RGB, and Mono.
 * \li One PDF file per rasterized page.
 * \li Output direct to disk or via back channel.
 * \li Flate (zlib) compression, and no compression.
 *
 * Alternative compressions are allowed by the PDF specification but no examples have been provided within this SDK;
 *
 * Adobe® Portable Document Format - Version 1.7 - November 2006\n
 * Standard filters\n
 * \li ASCIIHexDecode - Decodes data encoded in an ASCII hexadecimal representation, reproducing the original binary data.
 * \li ASCII85Decode - Decodes data encoded in an ASCII base-85 representation, reproducing the original binary data.
 * \li LZWDecode - Decompresses data encoded using the LZW (Lempel-Ziv-Welch) adaptive compression method, reproducing the original text or binary data.
 * \li FlateDecode - (PDF 1.2) Decompresses data encoded using the zlib/deflate compression method, reproducing the original text or binary data.
 * \li RunLengthDecode - Decompresses data encoded using a byte-oriented run-length encoding algorithm, reproducing the original text or binary data (typically monochrome image data, or any data that contains frequent long runs of a single byte value).
 * \li CCITTFaxDecode - Decompresses data encoded using the CCITT facsimile standard, reproducing the original data (typically monochrome image data at 1 bit per pixel).
 * \li JBIG2Decode - (PDF 1.4) Decompresses data encoded using the JBIG2 standard, reproducing the original monochrome (1 bit per pixel) image data (or an approximation of that data).
 * \li DCTDecode - Decompresses data encoded using a DCT (discrete cosine transform) technique based on the JPEG standard, reproducing image sample data that approximates the original data.
 * \li JPXDecode - (PDF 1.5) Decompresses data encoded using the wavelet-based JPEG2000 standard, reproducing the original image data.
 * \li Crypt - (PDF 1.5) Decrypts data encrypted by a security handler, reproducing the original data as it was before encryption.
 *
 *
 * The PDF version in the output is set to PDF 1.5 as this is the oldest version that supports 16 bits per pixel per colorant output.
 * If 16 bpp output is not required then you may consider using an older PDF version identifier to be compatible with older PDF readers.
 */
#include "pms.h"
#include "pms_malloc.h"
#include "pms_pdf_out.h"
#include "zlib.h"
#include <string.h>


/*! \brief Define to enable no compression. The default compression is flate (zlib). */
/* #define NOCOMPRESS */

/*! \brief Enabled this to test the compression algothrithm - especially useful when debugging third party code. */
/* #define COMPRESS_TEST */

/*! \brief Only required if ripping or external screen module does not clear
           padded pixels and if padded pixels are included in the output. */
#define NEED_TO_CLEAR_PADDED

/*! \brief The maximum number of PDF objects that are embedded in the PDF output file. */
#define MAXOBJ 20

/*! \brief Added comments to the PDF output file. */
#define ADD_PDF_COMMENTS

/*! \brief Memory allocation function to be used by the zlib library. */
static void *zlib_alloc(void *opaque, uInt items, uInt size);

/*! \brief Memory free function to be used by the zlib library. */
static void zlib_free(void *opaque, void *address);

/*! \brief Parameters for the PDF output module. */
typedef struct tagPDFout_globals {
  char *pRasterBuffer ;           /**< Pixel-interleaved raster data */
  unsigned int cbRasterBuffer;    /**< Raster buffer size */
  char *pCompressBuffer ;         /**< Compressed raster data */
  unsigned int cbCompressBuffer;  /**< Compression buffer size */

  FILE * hFileOut;                /**< Output file handle */
  PMS_TyBackChannelWriteFileOut tWriteFileOut; /**< Backchannel output */
  unsigned int nobjects;          /**< Number of objects in PDF file */
  unsigned int filepos;           /**< Current file position */
  unsigned int offsets[MAXOBJ] ;  /**< Location of PDF objects */
  struct z_stream_s zstate ;      /**< zlib context structure */
#ifdef ADD_PDF_COMMENTS
  unsigned int uPageChecksum;     /**< Checksum for comments for regression testing */
#endif
}TPDFOUT_GLOBALS, *PTPDFOUT_GLOBALS;

/*! PDF output module variables */
TPDFOUT_GLOBALS gtPDFOut = { NULL, 0, NULL, 0, NULL };

/**
 * \brief Open PDF data stream output.
 *
 * Output can be sent to the backchannel or direct to disk.
 *
 * \param pszFilename Filename (including path) of file to write, if file output, otherwise ignored.
 * \return TRUE if successful, FALSE otherwise.
 */
static int PDF_OpenOutput(const char *pszFilename)
{
  if(g_bBackChannelPageOutput)
  {
    return 1;
  }
  else
  {
    gtPDFOut.hFileOut = fopen(pszFilename, "wb");
    return (gtPDFOut.hFileOut!=NULL);
  }
}

/**
 * \brief Close PDF data stream output.
 *
 * Close the PDF output file if writing direct to file.
 * Free raster buffer and compression buffer used.
 */
static void PDF_CloseOutput()
{
  if(!g_bBackChannelPageOutput)
  {
    if(gtPDFOut.hFileOut)
    {
      fclose(gtPDFOut.hFileOut);
      gtPDFOut.hFileOut = NULL;
    }
  }

  if(gtPDFOut.pRasterBuffer)
  {
    OSFree(gtPDFOut.pRasterBuffer, PMS_MemoryPoolPMS);
    gtPDFOut.pRasterBuffer = NULL;
  }

  if(gtPDFOut.pCompressBuffer)
  {
    OSFree(gtPDFOut.pCompressBuffer, PMS_MemoryPoolPMS);
    gtPDFOut.pCompressBuffer = NULL;
  }
}

/**
 * \brief Write PDF data stream output.
 *
 * Write direct to disk or send to back channel.
 *
 * \param pBuffer Pointer to data to output.
 * \param nLength Number of bytes to output.
 * \return Number of bytes written.
 */
static int PDF_WriteOutput(char *pBuffer, int nLength)
{
  int nWritten = 0;
  int nResult = 0;
  if(g_bBackChannelPageOutput)
  {
    nResult = PMS_WriteDataStream(PMS_WRITE_FILE_OUT, &gtPDFOut.tWriteFileOut, (unsigned char *)pBuffer, nLength, &nWritten);
    PMS_ASSERT(nResult==0,("PDF_WriteOutput: Failed to output data... %d\n", nResult));
  }
  else
  {
    nWritten = (int)fwrite(pBuffer, 1, nLength, gtPDFOut.hFileOut);
  }
  return nWritten;
}

#ifdef ADD_PDF_COMMENTS
/**
 * \brief Write PDF data stream output.
 *
 * Write direct to disk or send to back channel, at a specific location in the file.
 * This is only used in the special case for adding a page checksum comment near the
 * start of the file so that regression testing can be performed quickly.
 *
 * \param pBuffer Pointer to data to output.
 * \param nLength Number of bytes to output.
 * \param nLength Number of bytes to output.
 * \return Number of bytes written.
 */
static int PDF_WriteOutputAtLoc(char *pBuffer, int nLength, unsigned long ulLocation)
{
  int nWritten = 0;
  int nResult = 0;
  if(g_bBackChannelPageOutput)
  {
    gtPDFOut.tWriteFileOut.uFlags = 1;
    gtPDFOut.tWriteFileOut.ulAbsPos = ulLocation;
    nResult = PMS_WriteDataStream(PMS_WRITE_FILE_OUT, &gtPDFOut.tWriteFileOut, (unsigned char *)pBuffer, nLength, &nWritten);
    PMS_ASSERT(nResult==0,("PDF_WriteOutput: Failed to output data... %d\n", nResult));
    gtPDFOut.tWriteFileOut.uFlags = 0;
    gtPDFOut.tWriteFileOut.ulAbsPos = 0;
  }
  else
  {
    long ulPos = ftell(gtPDFOut.hFileOut);
    fseek(gtPDFOut.hFileOut, (long)ulLocation, SEEK_SET);
    nWritten = (int)fwrite(pBuffer, 1, nLength, gtPDFOut.hFileOut);
    fseek(gtPDFOut.hFileOut, ulPos, SEEK_SET);
  }
  return nWritten;
}
#endif

/**
 * \brief Write PDF data stream header.
 *
 * Output the PDF file header.
 *
 * \param ptPMSPage Pointer to PMS page structure that contains the complete page.
 * \return PMS_ePDF_Errors error code.
 */
static int PDF_WriteFileHeader( PMS_TyPage *ptPMSPage )
{
  char buffer[512];
  int length ;
  char *pStr;
  static unsigned int nCurrentJobID = 0, nPageNo = 0;
  PMS_ePDF_Errors eResult = PDF_NoError;

  /* if its a new job reset page numbering */
  if(nCurrentJobID != ptPMSPage->JobId)
  {
    nCurrentJobID = ptPMSPage->JobId;
    nPageNo = 1;
  }

/* Global Graphics Regression Runner (QA test application) expects file name format "page-%p-%s.pdf", where %p is page number, and %s is colorant family or blank.
   The ebdwrapper Regression Runner BeanShell expects "%d-%d-%s.pdf", job number, page number, colorant family (rgb, cmyk, or k). The BeanShell renames the file
   to the format the RR expects.

   This line is commented out so that we can use Regression Runner with the ebdwrapper BeanShell without have to preprocess the filenames.
   Consider uncommenting to be consistent with TIFF output (FWIW TIFF output is tested with Global Graphics tiffdiff application).
   sprintf(gtPDFOut.tWriteFileOut.szFilename, "%s%d-%d.pdf", ptPMSPage->szJobName, ptPMSPage->JobId, nPageNo);
*/
  memset(gtPDFOut.tWriteFileOut.szFilename,0,sizeof(gtPDFOut.tWriteFileOut.szFilename));

#ifdef ADD_PDF_COMMENTS
  /* Initialise page checksum (used for regression testing) */
  gtPDFOut.uPageChecksum = 0;
#endif

  if(g_tSystemInfo.szOutputPath[0])
  {
    memcpy(gtPDFOut.tWriteFileOut.szFilename,g_tSystemInfo.szOutputPath,strlen(g_tSystemInfo.szOutputPath));
    pStr=strrchr(ptPMSPage->szJobName,'/');
    strcat(gtPDFOut.tWriteFileOut.szFilename,"/");
    if(pStr)
    {
      strcat(gtPDFOut.tWriteFileOut.szFilename,pStr+1);
    }
    else
    {
      strcat(gtPDFOut.tWriteFileOut.szFilename,ptPMSPage->szJobName);
    }
  }
  else
  {
    strncpy(gtPDFOut.tWriteFileOut.szFilename, ptPMSPage->szJobName, sizeof(gtPDFOut.tWriteFileOut.szFilename)-1);
  }

  pStr = gtPDFOut.tWriteFileOut.szFilename+strlen(gtPDFOut.tWriteFileOut.szFilename);
  while((pStr>gtPDFOut.tWriteFileOut.szFilename) && (*(pStr-1)!='/') && (*(pStr-1)!=':')) pStr--;

  /* Create a filename that is compatible with Global Graphics' Regression Runner test tool. */
  if ( ptPMSPage->uTotalPlanes == 1 )
    sprintf(pStr, "%d-%d-gray.pdf", ptPMSPage->JobId, nPageNo);
  else if ( ptPMSPage->uTotalPlanes == 4 )
    sprintf(pStr, "%d-%d-cmyk.pdf", ptPMSPage->JobId, nPageNo);
  else if ( ptPMSPage->uTotalPlanes == 3 )
    sprintf(pStr, "%d-%d-rgb.pdf", ptPMSPage->JobId, nPageNo);
  else
    sprintf(pStr, "%d-%d-.pdf", ptPMSPage->JobId, nPageNo);

  /* increment page number for next time */
  nPageNo++;

  gtPDFOut.nobjects = 0;
  gtPDFOut.filepos = 0;
  if(!PDF_OpenOutput(gtPDFOut.tWriteFileOut.szFilename))
  {
    PMS_SHOW_ERROR("PDF_WriteFileHeader: Failed to create file \"%s\" (Is it write protected or open in another application?)\n", gtPDFOut.tWriteFileOut.szFilename);
    eResult = PDF_Error_FileOpen;
    return eResult;
  }

  /* Write the PDF header, then the whole compressed raster, then the trailer.
   * We set the PDF version to 1.5 as we potentially could be outputting 16 bits per pixel (per colorant).
   * 16bpp support in PDF was introduced in PDF 1.5.
   */
  length = sprintf(buffer, "%%PDF-1.5\n"
                  "%%\343\342\317\323\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileHeader: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;

#ifdef ADD_PDF_COMMENTS
  {
    char szComment[1500];

    /* fill TIFF's comment buffer */
    sprintf(szComment,
      "%% ebdwrapper: Page Checksum: 0x12345678\n" /* overwritten at end */
      "%% ebdwrapper: %s \n"
      "%% ebdwrapper: Colorants: %d\n"
      "%% ebdwrapper: RIP Depth (bpp): %d\n"
      "%% ebdwrapper: Output Depth (bpp): %d\n"
      "%% ebdwrapper: Pixels Per Line (not inc. padding): %d\n"
      "%% ebdwrapper: Bytes Per Line (inc. padding): %d\n"
      "%% ebdwrapper: Bits Per Line (inc. padding): %d\n"
      "%% ebdwrapper: Lines Per Page: %d\n"
      "%% ebdwrapper: Job: %d Page: %d\n"
      "%% ebdwrapper: JobName: %s\n"
      "%% ebdwrapper: X dpi: %.2f\n"
      "%% ebdwrapper: Y dpi: %.2f\n"
      "%% ebdwrapper: Color Mode: %d\n"
      "%% ebdwrapper: Screen Mode: %d\n"
      "%% ebdwrapper: TotalPlanes: %d\n"
      "%% ebdwrapper: TotalBands: %d\n"
      "%% ebdwrapper: MediaType: %s\n"
      "%% ebdwrapper: MediaColor: %s\n"
      "%% ebdwrapper: MediaWeight: %d\n"
      "%% ebdwrapper: MediaSource: %d\n"
      "%% ebdwrapper: MediaWidth: %3.3f inches\n"
      "%% ebdwrapper: MediaHeight: %3.3f inches\n"
      "%% ebdwrapper: Duplex: %d\n"
      "%% ebdwrapper: Tumble: %d\n"
      "%% ebdwrapper: Collate: %d\n"
      "%% ebdwrapper: OutputFaceUp: %d\n"
      "%% ebdwrapper: Orientation: %d\n"
      "%% ebdwrapper: Output Tray: %d\n"
      ,
      gtPDFOut.tWriteFileOut.szFilename,
      ptPMSPage->uTotalPlanes,
      ptPMSPage->uRIPDepth,
      ptPMSPage->uOutputDepth,
      ptPMSPage->nPageWidthPixels,
      (ptPMSPage->nRasterWidthBits >> 3),
      ptPMSPage->nRasterWidthBits,
      ptPMSPage->nPageHeightPixels,
      ptPMSPage->JobId,
      ptPMSPage->PageId,
      ptPMSPage->szJobName,
      (float)ptPMSPage->dXResolution,
      (float)ptPMSPage->dYResolution,
      ptPMSPage->eColorMode,
      ptPMSPage->eScreenMode,
      ptPMSPage->uTotalPlanes,
      ptPMSPage->atPlane[3].uBandTotal,
      ptPMSPage->stMedia.szMediaType,
      ptPMSPage->stMedia.szMediaColor,
      ptPMSPage->stMedia.uMediaWeight,
      ptPMSPage->stMedia.uInputTray,
      (float)(ptPMSPage->stMedia.dWidth/72.0),
      (float)(ptPMSPage->stMedia.dHeight/72.0),
      ptPMSPage->bDuplex,
      ptPMSPage->bTumble,
      ptPMSPage->bCollate,
      ptPMSPage->bFaceUp,
      ptPMSPage->uOrientation,
      ptPMSPage->stMedia.eOutputTray
      );
    length = (int)strlen(szComment);
    if(PDF_WriteOutput(szComment, length) < length)
    {
      PMS_SHOW_ERROR("PDF_WriteFileHeader: File IO failed.\n");
      eResult = PDF_Error_FileIO;
      return eResult;
    }
    gtPDFOut.filepos += length ;
  }
#endif

  /* The offsets array stores the file location of every object in the file */
  gtPDFOut.offsets[gtPDFOut.nobjects++] = gtPDFOut.filepos ;

  /* The first PDF object is the raster page */
  length = sprintf(buffer,"1 0 obj\n"
                          "<<\n"
                          "  /Type /XObject\n"
                          "  /Subtype /Image\n"
                          "  /Width %d\n"
                          "  /Height %d\n"
                          "  /BitsPerComponent %d\n"
                          "  /Length 2 0 R\n"
                          "  /ColorSpace [\n",
                          ptPMSPage->nRasterWidthPixels, /* Not the original document page width. Instead this includes padding. */
                          ptPMSPage->nPageHeightPixels,
                          ptPMSPage->uOutputDepth);

  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileHeader: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;

  /* Write the color space */
  if ( ptPMSPage->uTotalPlanes == 1 )
    length = sprintf(buffer, "    /DeviceGray\n");
  else if ( ptPMSPage->uTotalPlanes == 4 )
    length = sprintf(buffer, "    /DeviceCMYK\n");
  else if ( ptPMSPage->uTotalPlanes == 3 )
    length = sprintf(buffer, "    /DeviceRGB\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileHeader: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;

  length = sprintf(buffer, "  ]\n"
#ifndef NOCOMPRESS
                           "  /Filter /FlateDecode\n" /* The raster page is compressed using the flate (zlib) alogorithm */
#endif
                           ">> stream\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileHeader: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;

  return eResult;
}

/**
 * \brief Write PDF data stream trailer.
 *
 * Output the final PDF output format.
 *
 * \param ptPMSPage Pointer to PMS page structure that contains the complete page.
 * \return PMS_ePDF_Errors error code.
 */
static int PDF_WriteFileTrailer(PMS_TyPage *ptPMSPage)
{
  int streamlength;
  char buffer[512];
  int length ;
  unsigned int i;
  float fRasterWidthPixels;
  PMS_ePDF_Errors eResult = PDF_NoError;

  length = sprintf(buffer, "\nendstream\n"
                           "endobj\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;
  gtPDFOut.offsets[gtPDFOut.nobjects++] = gtPDFOut.filepos ;

  /* The second object is the length of the image stream plus one for the new line char. */
  length = sprintf(buffer,    "2 0 obj\n" /* Length of image */
                              "%ld\n"
                              "endobj\n", gtPDFOut.zstate.total_out + 1);
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;
  gtPDFOut.offsets[gtPDFOut.nobjects++] = gtPDFOut.filepos ;

  length = sprintf(buffer,  "3 0 obj\n" /* Page resources */
                              "<<\n"
                              "  /XObject << /Im1 1 0 R >>\n"
                              "  /ProcSet [/PDF /ImageB /ImageC]\n"
                              ">>\n"
                              "endobj\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;
  gtPDFOut.offsets[gtPDFOut.nobjects++] = gtPDFOut.filepos ;

  length = sprintf(buffer,  "4 0 obj\n" /* Page contents stream */
                              "<< /Length 5 0 R >>\n"
                              "stream\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;

  if(ptPMSPage->pJobPMSData->eColorMode == PMS_RGB_PixelInterleaved) {
    fRasterWidthPixels = (float)((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) * 72.0f / ptPMSPage->dXResolution)/3.0f;
  } else {
    fRasterWidthPixels = (float)((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) * 72.0f / ptPMSPage->dXResolution);
  }

  streamlength = sprintf(buffer,  "%f 0 0 %f 0 0 cm /Im1 Do\n",
                                  fRasterWidthPixels,
                                  ptPMSPage->nPageHeightPixels * 72.0 / ptPMSPage->dYResolution);
  if(PDF_WriteOutput(buffer, streamlength) < streamlength)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += streamlength ;

  length = sprintf(buffer,  "endstream\n"
                            "endobj\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;
  gtPDFOut.offsets[gtPDFOut.nobjects++] = gtPDFOut.filepos ;

  length = sprintf(buffer,    "5 0 obj\n" /* Length of page contents stream */
                              "%d\n"
                              "endobj\n", streamlength);
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;
  gtPDFOut.offsets[gtPDFOut.nobjects++] = gtPDFOut.filepos ;

  length = sprintf(buffer,    "6 0 obj\n" /* Page object */
                              "<<\n"
                              "  /Type /Page\n"
                              "  /Resources 3 0 R\n"
                              "  /Contents 4 0 R\n"
                              "  /Parent 7 0 R\n"
                              "  /MediaBox [0 0 %.2f %.2f]\n"
                              ">>\n"
                              "endobj\n",
                              fRasterWidthPixels,
                              ptPMSPage->nPageHeightPixels * 72.0 / ptPMSPage->dYResolution);
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;
  gtPDFOut.offsets[gtPDFOut.nobjects++] = gtPDFOut.filepos ;

  /* We're outputting a single page per PDF. */
  length = sprintf(buffer,    "7 0 obj\n" /* Pages */
                              "<<\n"
                              "  /Type /Pages\n"
                              "  /Kids [ 6 0 R ]\n"
                              "  /Count 1\n"
                              ">>\n"
                              "endobj\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;
  gtPDFOut.offsets[gtPDFOut.nobjects++] = gtPDFOut.filepos ;

  length = sprintf(buffer,    "8 0 obj\n" /* Info */
                              "<<\n"
                              "  /Type /Catalog\n"
                              "  /Pages 7 0 R\n"
                              ">>\n"
                              "endobj\n");
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
  gtPDFOut.filepos += length ;

  length = sprintf(buffer,    "xref\n"
                              "0 %d\n"
                              "0000000000 65535 f\r\n",
                              gtPDFOut.nobjects + 1);
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }

  for ( i = 0 ; i < gtPDFOut.nobjects ; ++i )
  {
    length = sprintf(buffer, "%010d 00000 n\r\n",
                             gtPDFOut.offsets[i]);
    if(PDF_WriteOutput(buffer, length) < length)
    {
      PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
      eResult = PDF_Error_FileIO;
      return eResult;
    }
  }

  length = sprintf(buffer,    "trailer\n"
                              "<<\n"
                              "  /Size %d\n"
                              "  /Root 8 0 R\n"
                              ">>\n"
                              "startxref\n"
                              "%d\n"
                              "%%%%EOF\n",
                              gtPDFOut.nobjects + 1, gtPDFOut.filepos);
  if(PDF_WriteOutput(buffer, length) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }

#ifdef ADD_PDF_COMMENTS
  /* Page checksum embedded into comments for use with regression runner */
  length = sprintf((char*)buffer, "%08X", gtPDFOut.uPageChecksum);
  if(PDF_WriteOutputAtLoc(buffer, length, 46) < length)
  {
    PMS_SHOW_ERROR("PDF_WriteFileTrailer: File IO failed.\n");
    eResult = PDF_Error_FileIO;
    return eResult;
  }
#endif

  return eResult;
}

/**
 * \brief Pixel interleave for PDF file.
 *
 * The PDF spec requires that images (the raster page in our case) are stored as pixel interleaved.
 * The memory that pTo points to MUST be large enough to store the pixel interleaved band (there's
 * no overrun checking).
 *
 * \param ptPMSPage Pointer to PMS page structure that contains the complete page.
 * \param nBand Band to pixel interleave.
 * \param pTo Destination address.
 * \return Number of bytes written to destination.
 */
static unsigned int PDF_PixelInterleave(PMS_TyPage *ptPMSPage, int nBand, char *pTo)
{
  unsigned int nBytesToWritePerLine ;
  unsigned int i, nLinesThisBand;
  unsigned char *pSrc, *pDst, *ptColorBand[PMS_INVALID_COLOURANT];
  int swap;
  unsigned char uTmp;
  unsigned char bDoSwapBytesInWord;
  unsigned int cbSizeWritten = 0;
#ifdef NEED_TO_CLEAR_PADDED
  int x;
#endif

  /* set this to 1 if Bytes in a word need swapping */
  bDoSwapBytesInWord = 0;

  nBytesToWritePerLine = (ptPMSPage->nRasterWidthBits >> 3);
  /*  nBytesToWritePerLine = nBytesToWritePerLine+3)&~3); */

  if(ptPMSPage->uTotalPlanes == 1) /* Monochrome job handling */
  {
    nLinesThisBand = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].uBandHeight;

    /* This is the actual conversion to PDF format. The rest
       of the function is for endianess and padding */
    pSrc = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].pBandRaster;
    pDst = (unsigned char *)pTo;
    for(i=ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize; i > 0; --i)
    {
      *pDst = *pSrc ^ 0xFF;
      pDst++;
      pSrc++;
    }
    cbSizeWritten = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize;


#ifdef PMS_LOWBYTEFIRST
    if(ptPMSPage->uOutputDepth == 16)
    {
      pDst = (unsigned char*)pTo;
      for(i=0; i < ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize; i+=2)
      {
        uTmp = pDst[0];
        pDst[0] = pDst[1];
        pDst[1] = uTmp;
        pDst+=2;
      }
    }
#endif

    /* Do swap, if required */
    if(bDoSwapBytesInWord)
    {
      PMS_ASSERT((nBytesToWritePerLine&3)==0, ("PMS PDF_PageHandler function needs line lengths padded to 32 bit boundary.\n"));
      pDst = (unsigned char*)pTo;
      for(i=0; i < ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize; i+=4)
      {
        uTmp = pDst[0];
        pDst[0] = pDst[3];
        pDst[3] = uTmp;
        uTmp = pDst[1];
        pDst[1] = pDst[2];
        pDst[2] = uTmp;
        pDst+=4;
      }
    }

#ifdef NEED_TO_CLEAR_PADDED
    /* Clear padded pixels.
       The engine may not care about the padded data, but the data is
       written to the PDF output, so we will clear here, which will keep the binary
       output the same between runs. */
    if((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) > ptPMSPage->nPageWidthPixels)
    {
      unsigned char uMaskPartial; /* apply this mask to the partial bytes to be cleared */
      int nCountFull;             /* clear this number of full bytes */
      unsigned char aMask[8] = { 0xFF, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0xCF }; /* list of masks */
      unsigned int nOffset;
      unsigned int y;
      int nPaddedBits;

      nPaddedBits = ptPMSPage->nRasterWidthBits - (ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth);
      uMaskPartial = aMask[nPaddedBits % 8];
      nOffset = (ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) / 8;
      nCountFull = (ptPMSPage->nRasterWidthBits / 8) - nOffset - 1;

/*    printf("logical width bits/bytes = %d/%d:%d, physical width bits/bytes = %d/%d:%d, padded bits = %d, uMaskPartial = 0x%02x, nCountFull = %d, offset %d\n",
        ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth,
        (ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) / 8,
        (ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) & 0x0F,
        ptPMSPage->nRasterWidthBits,
        (ptPMSPage->nRasterWidthBits / 8),
        ptPMSPage->nRasterWidthBits & 0x0F,
        nPaddedBits,
        uMaskPartial,
        nCountFull,
        nOffset); */


      pDst = (unsigned char*)pTo + nOffset;
      for(y = nLinesThisBand; y > 0 ; y--)
      {
        *pDst|=uMaskPartial;
        for(x = 1; x <= nCountFull; x++) {
          pDst[x] = 0xFF;
        }
        pDst+=nBytesToWritePerLine;
      }
    }
#endif

  } /* end of monochrome job handling */
  else if(ptPMSPage->uTotalPlanes == 3)   /* RGB 8, 16, 4, 2 bpp job handling */
  {
    pDst = (unsigned char*)pTo;
    if(ptPMSPage->eColorMode == PMS_RGB_PixelInterleaved) {
      /* nBytesToWritePerLine * nLinesThisBand */
      memcpy(pDst, ptPMSPage->atPlane[0].atBand[nBand].pBandRaster, ptPMSPage->atPlane[0].atBand[nBand].cbBandSize);
      cbSizeWritten = ptPMSPage->atPlane[0].atBand[nBand].cbBandSize;
    } else {

    ptColorBand[PMS_RED] = ptPMSPage->atPlane[0].atBand[nBand].pBandRaster;
    ptColorBand[PMS_GREEN] = ptPMSPage->atPlane[1].atBand[nBand].pBandRaster;
    ptColorBand[PMS_BLUE] = ptPMSPage->atPlane[2].atBand[nBand].pBandRaster;

    nLinesThisBand = ptPMSPage->atPlane[0].atBand[nBand].uBandHeight;
    do
    {

      /* start of new line */
      switch(ptPMSPage->uOutputDepth)
      {
      case 8:
          for(i=0; i < (unsigned int)ptPMSPage->nPageWidthPixels; i++)
          {
            *pDst++ = (unsigned char)(*ptColorBand[PMS_RED]++) ; /* for 8 bits */
            *pDst++ = (unsigned char)(*ptColorBand[PMS_GREEN]++) ; /* for 8 bits */
            *pDst++ = (unsigned char)(*ptColorBand[PMS_BLUE]++) ; /* for 8 bits */
          }
#ifdef NEED_TO_CLEAR_PADDED
          for(i=ptPMSPage->nPageWidthPixels; i < (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i++)
          {
            *pDst++ = 0xff;
            *pDst++ = 0xff;
            *pDst++ = 0xff;
            ptColorBand[PMS_RED]++;
            ptColorBand[PMS_GREEN]++;
            ptColorBand[PMS_BLUE]++;
          }
#endif
        break;
      case 16:
        for(i=0; i < (unsigned int)(ptPMSPage->nPageWidthPixels); i++)
        {
#ifdef PMS_LOWBYTEFIRST
          {
            /* Swap bytes if on a little-endian platform. */
            *pDst++ = (unsigned char)(*(ptColorBand[PMS_RED]+1)) ;
            *pDst++ = (unsigned char)(*ptColorBand[PMS_RED]) ;
            *pDst++ = (unsigned char)(*(ptColorBand[PMS_GREEN]+1)) ;
            *pDst++ = (unsigned char)(*ptColorBand[PMS_GREEN]) ;
            *pDst++ = (unsigned char)(*(ptColorBand[PMS_BLUE]+1)) ;
            *pDst++ = (unsigned char)(*ptColorBand[PMS_BLUE]) ;
            ptColorBand[PMS_RED]+= 2;
            ptColorBand[PMS_GREEN]+= 2;
            ptColorBand[PMS_BLUE]+= 2;
          }
#else
          {
            *pDst++ = (unsigned char)(*ptColorBand[PMS_RED]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[PMS_RED]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[PMS_GREEN]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[PMS_GREEN]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[PMS_BLUE]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[PMS_BLUE]++) ;
          }
#endif
        }
#ifdef NEED_TO_CLEAR_PADDED
        for(; i < (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i++)
        {
          *pDst++ = 0xff;
          *pDst++ = 0xff;
          *pDst++ = 0xff;
          *pDst++ = 0xff;
          *pDst++ = 0xff;
          *pDst++ = 0xff;
          ptColorBand[PMS_RED]+= 2;
          ptColorBand[PMS_GREEN]+= 2;
          ptColorBand[PMS_BLUE]+= 2;
        }
#endif
        break;
      case 4:
        for(i=1; i <= (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i=i+2)
        {
          /* in each iteration, pickup 2 pixels from each RGB plane and make 3 bytes - RG BR GB at destination */
          *pDst++ = (((*ptColorBand[PMS_RED])& 0xF0)|(((*ptColorBand[PMS_GREEN])& 0xF0)>>4));
          *pDst++ = (((*ptColorBand[PMS_BLUE])& 0xF0)|(((*ptColorBand[PMS_RED])& 0x0F)));
          *pDst++ = ((((*ptColorBand[PMS_GREEN])& 0x0F)<<4)|((*ptColorBand[PMS_BLUE])& 0x0F));
          ptColorBand[PMS_RED] ++;
          ptColorBand[PMS_GREEN] ++;
          ptColorBand[PMS_BLUE] ++;
        }
        /* Clear the padded pixels as the external screening module nor the rip clear them
           for 4bpp (nor 2bpp) */
        for(i = (unsigned int)(((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) -
                                ptPMSPage->nPageWidthPixels) * 2);
            i > 0;
            i --)
        {
          /* To set to padded pixels to light magenta
             to[-(int)i] = 0x10;
           */
          pDst[-(int)i] = 0x00;
        }
        break;
      case 2:
        for(i=0; i < (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i=i+4)
        {
         /* in each iteration, pickup 4 pixels from each RGB plane and make 3 bytes - RGBR GBRG BRGB at destination */
            *pDst++ = ( (((*ptColorBand[PMS_RED])& 0xc0)) |
                      (((*ptColorBand[PMS_GREEN])& 0xc0)>>2) |
                      (((*ptColorBand[PMS_BLUE])& 0xc0)>>4) |
                      (((*ptColorBand[PMS_RED])& 0x30)>>4) );

            *pDst++ = ( (((*ptColorBand[PMS_GREEN])& 0x30)<<2) |
                      (((*ptColorBand[PMS_BLUE])& 0x30)) |
                      (((*ptColorBand[PMS_RED])& 0x0c)) |
                      (((*ptColorBand[PMS_GREEN])& 0x0c)>>2) );

            *pDst++ = ( (((*ptColorBand[PMS_BLUE])& 0x0c)<<4) |
                      (((*ptColorBand[PMS_RED])& 0x03)<<4) |
                      (((*ptColorBand[PMS_GREEN])& 0x03)<<2) |
                      (((*ptColorBand[PMS_BLUE])& 0x03)) );

            ptColorBand[PMS_RED]++;
            ptColorBand[PMS_GREEN]++;
            ptColorBand[PMS_BLUE]++;
        }

#ifdef NEED_TO_CLEAR_PADDED
        /* Clear the padded pixels as the external screening module nor
           the rip clear them for 2bpp (nor 4bpp) */
        for(i = (unsigned int)((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) -
                                ptPMSPage->nPageWidthPixels);
            i > 0;
            i --)
        {
          /* To set to padded pixels to light magenta
             to[-(int)i] = 0x10;
           */
          pDst[-(int)i] = 0x00;
        }
#endif
        break;
      case 1:
      default:
        PMS_SHOW_ERROR("RGB %d bpp is not yet supported in PDF out.\n", ptPMSPage->uOutputDepth);
        return FALSE;
        break;
      }
      nLinesThisBand -= 1 ;
    } while ( nLinesThisBand > 0 ) ;
    cbSizeWritten = ptPMSPage->atPlane[0].atBand[nBand].cbBandSize * 3;
    }
  }/* end of RGb 8, 16, 2, 4 bpp job handling */
  else if(ptPMSPage->uOutputDepth != 1)   /* CMYK 8,16, 4, 2 bpp job handling */
  {
    pDst = (unsigned char*)pTo;

    ptColorBand[PMS_CYAN] = ptPMSPage->atPlane[PMS_CYAN].atBand[nBand].pBandRaster;
    ptColorBand[PMS_MAGENTA] = ptPMSPage->atPlane[PMS_MAGENTA].atBand[nBand].pBandRaster;
    ptColorBand[PMS_YELLOW] = ptPMSPage->atPlane[PMS_YELLOW].atBand[nBand].pBandRaster;
    ptColorBand[PMS_BLACK] = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].pBandRaster;

    nLinesThisBand = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].uBandHeight;
    do
    {
      /* start of new line */
      switch(ptPMSPage->uOutputDepth)
      {
      case 8:
        if(bDoSwapBytesInWord)
        {
          for(i=0; i < ((unsigned int)(ptPMSPage->nPageWidthPixels)&~3); i+=4)
          {
            *pDst++ = (unsigned char)(ptColorBand[0][3]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[1][3]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[2][3]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[3][3]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[0][2]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[1][2]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[2][2]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[3][2]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[0][1]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[1][1]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[2][1]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[3][1]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[0][0]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[1][0]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[2][0]) ; /* for 8 bits */
            *pDst++ = (unsigned char)(ptColorBand[3][0]) ; /* for 8 bits */

            ptColorBand[0]+=4;
            ptColorBand[1]+=4;
            ptColorBand[2]+=4;
            ptColorBand[3]+=4;
          }
        }
        else
        {
          for(i=0; i < (unsigned int)(ptPMSPage->nPageWidthPixels); i++)
          {
            *pDst++ = (unsigned char)(*ptColorBand[0]++) ; /* for 8 bits */
            *pDst++ = (unsigned char)(*ptColorBand[1]++) ; /* for 8 bits */
            *pDst++ = (unsigned char)(*ptColorBand[2]++) ; /* for 8 bits */
            *pDst++ = (unsigned char)(*ptColorBand[3]++) ; /* for 8 bits */
          }
        }
#ifdef NEED_TO_CLEAR_PADDED
        for(; i < (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i++)
        {
          *pDst++ = 0;
          *pDst++ = 0;
          *pDst++ = 0;
          *pDst++ = 0;
          ptColorBand[0]++;
          ptColorBand[1]++;
          ptColorBand[2]++;
          ptColorBand[3]++;
        }
#endif
        break;
      case 16:
        PMS_ASSERT(bDoSwapBytesInWord==0,
          ("Swap not supported. Do we ever have a case for swapping bytes in a word for 16bpp output?\n"));
        for(i=0; i < (unsigned int)(ptPMSPage->nPageWidthPixels); i++)
        {
#ifdef PMS_LOWBYTEFIRST
          {
            /* Swap bytes if on a little-endian platform. */
            *pDst++ = (unsigned char)(*(ptColorBand[0]+1)) ;
            *pDst++ = (unsigned char)(*ptColorBand[0]) ;
            *pDst++ = (unsigned char)(*(ptColorBand[1]+1)) ;
            *pDst++ = (unsigned char)(*ptColorBand[1]) ;
            *pDst++ = (unsigned char)(*(ptColorBand[2]+1)) ;
            *pDst++ = (unsigned char)(*ptColorBand[2]) ;
            *pDst++ = (unsigned char)(*(ptColorBand[3]+1)) ;
            *pDst++ = (unsigned char)(*ptColorBand[3]) ;
            ptColorBand[0]+= 2;
            ptColorBand[1]+= 2;
            ptColorBand[2]+= 2;
            ptColorBand[3]+= 2;
          }
#else
          {
            *pDst++ = (unsigned char)(*ptColorBand[0]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[0]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[1]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[1]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[2]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[2]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[3]++) ;
            *pDst++ = (unsigned char)(*ptColorBand[3]++) ;
          }
#endif
        }
#ifdef NEED_TO_CLEAR_PADDED
        for(; i < (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i++)
        {
          *pDst++ = 0;
          *pDst++ = 0;
          *pDst++ = 0;
          *pDst++ = 0;
          *pDst++ = 0;
          *pDst++ = 0;
          *pDst++ = 0;
          *pDst++ = 0;
          ptColorBand[0]+= 2;
          ptColorBand[1]+= 2;
          ptColorBand[2]+= 2;
          ptColorBand[3]+= 2;
        }
#endif
        break;
      case 4:
        if(bDoSwapBytesInWord)
        {
          for(i=0; i < (unsigned int)((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) & ~3); i+=8)
          {
            *pDst++ = (((ptColorBand[0][3])& 0xF0)|(((ptColorBand[1][3])& 0xF0)>>4));
            *pDst++ = (((ptColorBand[2][3])& 0xF0)|(((ptColorBand[3][3])& 0xF0)>>4));

            *pDst++ = ((((ptColorBand[0][3])& 0x0F)<<4)|((ptColorBand[1][3])& 0x0F));
            *pDst++ = ((((ptColorBand[2][3])& 0x0F)<<4)|((ptColorBand[3][3])& 0x0F));

            *pDst++ = (((ptColorBand[0][2])& 0xF0)|(((ptColorBand[1][2])& 0xF0)>>4));
            *pDst++ = (((ptColorBand[2][2])& 0xF0)|(((ptColorBand[3][2])& 0xF0)>>4));

            *pDst++ = ((((ptColorBand[0][2])& 0x0F)<<4)|((ptColorBand[1][2])& 0x0F));
            *pDst++ = ((((ptColorBand[2][2])& 0x0F)<<4)|((ptColorBand[3][2])& 0x0F));

            *pDst++ = (((ptColorBand[0][1])& 0xF0)|(((ptColorBand[1][1])& 0xF0)>>4));
            *pDst++ = (((ptColorBand[2][1])& 0xF0)|(((ptColorBand[3][1])& 0xF0)>>4));

            *pDst++ = ((((ptColorBand[0][1])& 0x0F)<<4)|((ptColorBand[1][1])& 0x0F));
            *pDst++ = ((((ptColorBand[2][1])& 0x0F)<<4)|((ptColorBand[3][1])& 0x0F));

            *pDst++ = (((ptColorBand[0][0])& 0xF0)|(((ptColorBand[1][0])& 0xF0)>>4));
            *pDst++ = (((ptColorBand[2][0])& 0xF0)|(((ptColorBand[3][0])& 0xF0)>>4));

            *pDst++ = ((((ptColorBand[0][0])& 0x0F)<<4)|((ptColorBand[1][0])& 0x0F));
            *pDst++ = ((((ptColorBand[2][0])& 0x0F)<<4)|((ptColorBand[3][0])& 0x0F));

            ptColorBand[0]+=4;
            ptColorBand[1]+=4;
            ptColorBand[2]+=4;
            ptColorBand[3]+=4;
          }
        }
        else
        {
          for(i=1; i <= (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i++)
          {
            if(i%2)
            {
              *pDst++ = (((*ptColorBand[0])& 0xF0)|(((*ptColorBand[1])& 0xF0)>>4));
              *pDst++ = (((*ptColorBand[2])& 0xF0)|(((*ptColorBand[3])& 0xF0)>>4));
            }
            else
            {
              *pDst++ = ((((*ptColorBand[0])& 0x0F)<<4)|((*ptColorBand[1])& 0x0F));
              *pDst++ = ((((*ptColorBand[2])& 0x0F)<<4)|((*ptColorBand[3])& 0x0F));

              ptColorBand[0]++;
              ptColorBand[1]++;
              ptColorBand[2]++;
              ptColorBand[3]++;
            }
          }
        }
#ifdef NEED_TO_CLEAR_PADDED
        /* Clear the padded pixels */
        for(i = (unsigned int)(((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) -
                                ptPMSPage->nPageWidthPixels) * 2);
            i > 0;
            i --)
        {
          /* To set to padded pixels to light magenta
             to[-(int)i] = 0x10;
           */
          pDst[-(int)i] = 0x00;
        }
#endif
        break;
      case 2:
        if(bDoSwapBytesInWord)
        {
          swap = 0;
          for(i=0; i < (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i++)
          {
            int cell = i&3;
            if((i&15)==0)
              swap = 3;
            switch(cell)
            {
            case 0:
              *pDst++ = ( (((ptColorBand[0][swap])& 0xc0)) |
                        (((ptColorBand[1][swap])& 0xc0)>>2) |
                        (((ptColorBand[2][swap])& 0xc0)>>4) |
                        (((ptColorBand[3][swap])& 0xc0)>>6) );
              break;
            case 1:
              *pDst++ = ( (((ptColorBand[0][swap])& 0x30)<<2) |
                        (((ptColorBand[1][swap])& 0x30)) |
                        (((ptColorBand[2][swap])& 0x30)>>2) |
                        (((ptColorBand[3][swap])& 0x30)>>4) );
              break;
            case 2:
              *pDst++ = ( (((ptColorBand[0][swap])& 0x0c)<<4) |
                        (((ptColorBand[1][swap])& 0x0c)<<2) |
                        (((ptColorBand[2][swap])& 0x0c)) |
                        (((ptColorBand[3][swap])& 0x0c)>>2) );
              break;
            case 3:
              *pDst++ = ( (((ptColorBand[0][swap])& 0x03)<<6) |
                        (((ptColorBand[1][swap])& 0x03)<<4) |
                        (((ptColorBand[2][swap])& 0x03)<<2) |
                        (((ptColorBand[3][swap])& 0x03)) );
              swap--;
              break;
            default:
              break;
            }
            if((i&15)==15)
            {
              ptColorBand[0]+=4;
              ptColorBand[1]+=4;
              ptColorBand[2]+=4;
              ptColorBand[3]+=4;
            }
          }
        }
        else
        {
          for(i=0; i < (unsigned int)(ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth); i++)
          {
            int cell = i%4;
            switch(cell)
            {
            case 0:
              *pDst++ = ( (((*ptColorBand[0])& 0xc0)) |
                        (((*ptColorBand[1])& 0xc0)>>2) |
                        (((*ptColorBand[2])& 0xc0)>>4) |
                        (((*ptColorBand[3])& 0xc0)>>6) );
              break;
            case 1:
              *pDst++ = ( (((*ptColorBand[0])& 0x30)<<2) |
                        (((*ptColorBand[1])& 0x30)) |
                        (((*ptColorBand[2])& 0x30)>>2) |
                        (((*ptColorBand[3])& 0x30)>>4) );
              break;
            case 2:
              *pDst++ = ( (((*ptColorBand[0])& 0x0c)<<4) |
                        (((*ptColorBand[1])& 0x0c)<<2) |
                        (((*ptColorBand[2])& 0x0c)) |
                        (((*ptColorBand[3])& 0x0c)>>2) );
              break;
            case 3:
              *pDst++ = ( (((*ptColorBand[0])& 0x03)<<6) |
                        (((*ptColorBand[1])& 0x3)<<4) |
                        (((*ptColorBand[2])& 0x03)<<2) |
                        (((*ptColorBand[3])& 0x03)) );

              ptColorBand[0]++;
              ptColorBand[1]++;
              ptColorBand[2]++;
              ptColorBand[3]++;
              break;
            default:
              break;
            }
          }
        }

#ifdef NEED_TO_CLEAR_PADDED
        /* Clear the padded pixels */
        for(i = (unsigned int)((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) -
                                ptPMSPage->nPageWidthPixels);
            i > 0;
            i --)
        {
          /* To set to padded pixels to light magenta
             to[-(int)i] = 0x10;
           */
          pDst[-(int)i] = 0x00;
        }
#endif
        break;
      default:
        break;
      }
      nLinesThisBand -= 1 ;
    } while ( nLinesThisBand > 0 ) ;
    cbSizeWritten = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize * 4;
  }/* end of CMYK 8, 16 bpp job handling */
  else if(ptPMSPage->uOutputDepth == 1)   /* CMYK 1 bpp job handling */
  {
    pDst = (unsigned char*)pTo;

    /* start of new band */
    ptColorBand[PMS_CYAN] = ptPMSPage->atPlane[PMS_CYAN].atBand[nBand].pBandRaster;
    ptColorBand[PMS_MAGENTA] = ptPMSPage->atPlane[PMS_MAGENTA].atBand[nBand].pBandRaster;
    ptColorBand[PMS_YELLOW] = ptPMSPage->atPlane[PMS_YELLOW].atBand[nBand].pBandRaster;
    ptColorBand[PMS_BLACK] = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].pBandRaster;

    /* Do swap, if required */
    if(bDoSwapBytesInWord)
    {
      PMS_ASSERT((ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize&3)==0,("PDF_PixelInterleave(), band size needs to be a multiple of 4 bytes\n"));

      /* CMYKCMYKCMYKCMYKCMYKCMYKCMYKCMYK */
      for(i=0; i < (ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize); i+=4)
      {
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][3] & 0x80) >> 0) | ((ptColorBand[PMS_MAGENTA][3] & 0x80) >> 1) | ((ptColorBand[PMS_YELLOW][3] & 0x80) >> 2) | ((ptColorBand[PMS_BLACK][3] & 0x80) >> 3) |
          ((ptColorBand[PMS_CYAN][3] & 0x40) >> 3) | ((ptColorBand[PMS_MAGENTA][3] & 0x40) >> 4) | ((ptColorBand[PMS_YELLOW][3] & 0x40) >> 5) | ((ptColorBand[PMS_BLACK][3] & 0x40) >> 6)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][3] & 0x20) << 2) | ((ptColorBand[PMS_MAGENTA][3] & 0x20) << 1) | ((ptColorBand[PMS_YELLOW][3] & 0x20) >> 0) | ((ptColorBand[PMS_BLACK][3] & 0x20) >> 1) |
          ((ptColorBand[PMS_CYAN][3] & 0x10) >> 1) | ((ptColorBand[PMS_MAGENTA][3] & 0x10) >> 2) | ((ptColorBand[PMS_YELLOW][3] & 0x10) >> 3) | ((ptColorBand[PMS_BLACK][3] & 0x10) >> 4)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][3] & 0x08) << 4) | ((ptColorBand[PMS_MAGENTA][3] & 0x08) << 3) | ((ptColorBand[PMS_YELLOW][3] & 0x08) << 2) | ((ptColorBand[PMS_BLACK][3] & 0x08) << 1) |
          ((ptColorBand[PMS_CYAN][3] & 0x04) << 1) | ((ptColorBand[PMS_MAGENTA][3] & 0x04) << 0) | ((ptColorBand[PMS_YELLOW][3] & 0x04) >> 1) | ((ptColorBand[PMS_BLACK][3] & 0x04) >> 2)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][3] & 0x02) << 6) | ((ptColorBand[PMS_MAGENTA][3] & 0x02) << 5) | ((ptColorBand[PMS_YELLOW][3] & 0x02) << 4) | ((ptColorBand[PMS_BLACK][3] & 0x02) << 3) |
          ((ptColorBand[PMS_CYAN][3] & 0x01) << 3) | ((ptColorBand[PMS_MAGENTA][3] & 0x01) << 2) | ((ptColorBand[PMS_YELLOW][3] & 0x01) << 1) | ((ptColorBand[PMS_BLACK][3] & 0x01) >> 0)
          );

        *pDst++ = (
          ((ptColorBand[PMS_CYAN][2] & 0x80) >> 0) | ((ptColorBand[PMS_MAGENTA][2] & 0x80) >> 1) | ((ptColorBand[PMS_YELLOW][2] & 0x80) >> 2) | ((ptColorBand[PMS_BLACK][2] & 0x80) >> 3) |
          ((ptColorBand[PMS_CYAN][2] & 0x40) >> 3) | ((ptColorBand[PMS_MAGENTA][2] & 0x40) >> 4) | ((ptColorBand[PMS_YELLOW][2] & 0x40) >> 5) | ((ptColorBand[PMS_BLACK][2] & 0x40) >> 6)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][2] & 0x20) << 2) | ((ptColorBand[PMS_MAGENTA][2] & 0x20) << 1) | ((ptColorBand[PMS_YELLOW][2] & 0x20) >> 0) | ((ptColorBand[PMS_BLACK][2] & 0x20) >> 1) |
          ((ptColorBand[PMS_CYAN][2] & 0x10) >> 1) | ((ptColorBand[PMS_MAGENTA][2] & 0x10) >> 2) | ((ptColorBand[PMS_YELLOW][2] & 0x10) >> 3) | ((ptColorBand[PMS_BLACK][2] & 0x10) >> 4)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][2] & 0x08) << 4) | ((ptColorBand[PMS_MAGENTA][2] & 0x08) << 3) | ((ptColorBand[PMS_YELLOW][2] & 0x08) << 2) | ((ptColorBand[PMS_BLACK][2] & 0x08) << 1) |
          ((ptColorBand[PMS_CYAN][2] & 0x04) << 1) | ((ptColorBand[PMS_MAGENTA][2] & 0x04) << 0) | ((ptColorBand[PMS_YELLOW][2] & 0x04) >> 1) | ((ptColorBand[PMS_BLACK][2] & 0x04) >> 2)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][2] & 0x02) << 6) | ((ptColorBand[PMS_MAGENTA][2] & 0x02) << 5) | ((ptColorBand[PMS_YELLOW][2] & 0x02) << 4) | ((ptColorBand[PMS_BLACK][2] & 0x02) << 3) |
          ((ptColorBand[PMS_CYAN][2] & 0x01) << 3) | ((ptColorBand[PMS_MAGENTA][2] & 0x01) << 2) | ((ptColorBand[PMS_YELLOW][2] & 0x01) << 1) | ((ptColorBand[PMS_BLACK][2] & 0x01) >> 0)
          );

        *pDst++ = (
          ((ptColorBand[PMS_CYAN][1] & 0x80) >> 0) | ((ptColorBand[PMS_MAGENTA][1] & 0x80) >> 1) | ((ptColorBand[PMS_YELLOW][1] & 0x80) >> 2) | ((ptColorBand[PMS_BLACK][1] & 0x80) >> 3) |
          ((ptColorBand[PMS_CYAN][1] & 0x40) >> 3) | ((ptColorBand[PMS_MAGENTA][1] & 0x40) >> 4) | ((ptColorBand[PMS_YELLOW][1] & 0x40) >> 5) | ((ptColorBand[PMS_BLACK][1] & 0x40) >> 6)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][1] & 0x20) << 2) | ((ptColorBand[PMS_MAGENTA][1] & 0x20) << 1) | ((ptColorBand[PMS_YELLOW][1] & 0x20) >> 0) | ((ptColorBand[PMS_BLACK][1] & 0x20) >> 1) |
          ((ptColorBand[PMS_CYAN][1] & 0x10) >> 1) | ((ptColorBand[PMS_MAGENTA][1] & 0x10) >> 2) | ((ptColorBand[PMS_YELLOW][1] & 0x10) >> 3) | ((ptColorBand[PMS_BLACK][1] & 0x10) >> 4)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][1] & 0x08) << 4) | ((ptColorBand[PMS_MAGENTA][1] & 0x08) << 3) | ((ptColorBand[PMS_YELLOW][1] & 0x08) << 2) | ((ptColorBand[PMS_BLACK][1] & 0x08) << 1) |
          ((ptColorBand[PMS_CYAN][1] & 0x04) << 1) | ((ptColorBand[PMS_MAGENTA][1] & 0x04) << 0) | ((ptColorBand[PMS_YELLOW][1] & 0x04) >> 1) | ((ptColorBand[PMS_BLACK][1] & 0x04) >> 2)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][1] & 0x02) << 6) | ((ptColorBand[PMS_MAGENTA][1] & 0x02) << 5) | ((ptColorBand[PMS_YELLOW][1] & 0x02) << 4) | ((ptColorBand[PMS_BLACK][1] & 0x02) << 3) |
          ((ptColorBand[PMS_CYAN][1] & 0x01) << 3) | ((ptColorBand[PMS_MAGENTA][1] & 0x01) << 2) | ((ptColorBand[PMS_YELLOW][1] & 0x01) << 1) | ((ptColorBand[PMS_BLACK][1] & 0x01) >> 0)
          );

        *pDst++ = (
          ((ptColorBand[PMS_CYAN][0] & 0x80) >> 0) | ((ptColorBand[PMS_MAGENTA][0] & 0x80) >> 1) | ((ptColorBand[PMS_YELLOW][0] & 0x80) >> 2) | ((ptColorBand[PMS_BLACK][0] & 0x80) >> 3) |
          ((ptColorBand[PMS_CYAN][0] & 0x40) >> 3) | ((ptColorBand[PMS_MAGENTA][0] & 0x40) >> 4) | ((ptColorBand[PMS_YELLOW][0] & 0x40) >> 5) | ((ptColorBand[PMS_BLACK][0] & 0x40) >> 6)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][0] & 0x20) << 2) | ((ptColorBand[PMS_MAGENTA][0] & 0x20) << 1) | ((ptColorBand[PMS_YELLOW][0] & 0x20) >> 0) | ((ptColorBand[PMS_BLACK][0] & 0x20) >> 1) |
          ((ptColorBand[PMS_CYAN][0] & 0x10) >> 1) | ((ptColorBand[PMS_MAGENTA][0] & 0x10) >> 2) | ((ptColorBand[PMS_YELLOW][0] & 0x10) >> 3) | ((ptColorBand[PMS_BLACK][0] & 0x10) >> 4)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][0] & 0x08) << 4) | ((ptColorBand[PMS_MAGENTA][0] & 0x08) << 3) | ((ptColorBand[PMS_YELLOW][0] & 0x08) << 2) | ((ptColorBand[PMS_BLACK][0] & 0x08) << 1) |
          ((ptColorBand[PMS_CYAN][0] & 0x04) << 1) | ((ptColorBand[PMS_MAGENTA][0] & 0x04) << 0) | ((ptColorBand[PMS_YELLOW][0] & 0x04) >> 1) | ((ptColorBand[PMS_BLACK][0] & 0x04) >> 2)
          );
        *pDst++ = (
          ((ptColorBand[PMS_CYAN][0] & 0x02) << 6) | ((ptColorBand[PMS_MAGENTA][0] & 0x02) << 5) | ((ptColorBand[PMS_YELLOW][0] & 0x02) << 4) | ((ptColorBand[PMS_BLACK][0] & 0x02) << 3) |
          ((ptColorBand[PMS_CYAN][0] & 0x01) << 3) | ((ptColorBand[PMS_MAGENTA][0] & 0x01) << 2) | ((ptColorBand[PMS_YELLOW][0] & 0x01) << 1) | ((ptColorBand[PMS_BLACK][0] & 0x01) >> 0)
          );

        ptColorBand[PMS_CYAN]+=4;
        ptColorBand[PMS_MAGENTA]+=4;
        ptColorBand[PMS_YELLOW]+=4;
        ptColorBand[PMS_BLACK]+=4;
      }
    }
    else
    {
      /* CMYKCMYKCMYKCMYKCMYKCMYKCMYKCMYK */
      for(i=0; i < (ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize); i++)
      {
        *pDst++ = (
          ((*ptColorBand[PMS_CYAN] & 0x80) >> 0) | ((*ptColorBand[PMS_MAGENTA] & 0x80) >> 1) | ((*ptColorBand[PMS_YELLOW] & 0x80) >> 2) | ((*ptColorBand[PMS_BLACK] & 0x80) >> 3) |
          ((*ptColorBand[PMS_CYAN] & 0x40) >> 3) | ((*ptColorBand[PMS_MAGENTA] & 0x40) >> 4) | ((*ptColorBand[PMS_YELLOW] & 0x40) >> 5) | ((*ptColorBand[PMS_BLACK] & 0x40) >> 6)
          );
        *pDst++ = (
          ((*ptColorBand[PMS_CYAN] & 0x20) << 2) | ((*ptColorBand[PMS_MAGENTA] & 0x20) << 1) | ((*ptColorBand[PMS_YELLOW] & 0x20) >> 0) | ((*ptColorBand[PMS_BLACK] & 0x20) >> 1) |
          ((*ptColorBand[PMS_CYAN] & 0x10) >> 1) | ((*ptColorBand[PMS_MAGENTA] & 0x10) >> 2) | ((*ptColorBand[PMS_YELLOW] & 0x10) >> 3) | ((*ptColorBand[PMS_BLACK] & 0x10) >> 4)
          );
        *pDst++ = (
          ((*ptColorBand[PMS_CYAN] & 0x08) << 4) | ((*ptColorBand[PMS_MAGENTA] & 0x08) << 3) | ((*ptColorBand[PMS_YELLOW] & 0x08) << 2) | ((*ptColorBand[PMS_BLACK] & 0x08) << 1) |
          ((*ptColorBand[PMS_CYAN] & 0x04) << 1) | ((*ptColorBand[PMS_MAGENTA] & 0x04) << 0) | ((*ptColorBand[PMS_YELLOW] & 0x04) >> 1) | ((*ptColorBand[PMS_BLACK] & 0x04) >> 2)
          );
        *pDst++ = (
          ((*ptColorBand[PMS_CYAN] & 0x02) << 6) | ((*ptColorBand[PMS_MAGENTA] & 0x02) << 5) | ((*ptColorBand[PMS_YELLOW] & 0x02) << 4) | ((*ptColorBand[PMS_BLACK] & 0x02) << 3) |
          ((*ptColorBand[PMS_CYAN] & 0x01) << 3) | ((*ptColorBand[PMS_MAGENTA] & 0x01) << 2) | ((*ptColorBand[PMS_YELLOW] & 0x01) << 1) | ((*ptColorBand[PMS_BLACK] & 0x01) >> 0)
          );
        ptColorBand[PMS_CYAN]++;
        ptColorBand[PMS_MAGENTA]++;
        ptColorBand[PMS_YELLOW]++;
        ptColorBand[PMS_BLACK]++;
      }
    }
    cbSizeWritten = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].cbBandSize * 4;

#ifdef NEED_TO_CLEAR_PADDED
    /* Clear padded pixels.
       The engine may not care about the padded data, but the data is
       written to the PDF output, so we will clear here, which will keep the binary
       output the same between runs. */
    if((ptPMSPage->nRasterWidthBits / ptPMSPage->uOutputDepth) > ptPMSPage->nPageWidthPixels)
    {
      unsigned char uMaskPartial; /* apply this mask to the partial bytes to be cleared */
      int nCountFull;             /* clear this number of full bytes */
      unsigned char aMask[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xF0, 0xF0, 0xF0 }; /* list of masks */
      unsigned int nOffset;
      unsigned int y;
      int nPaddedBits;

      /* if a single colorant had 30 padded bits, the result will be 4 * 30 padded bits */
      nPaddedBits = ptPMSPage->nRasterWidthBits - (ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth);

      nPaddedBits *= 4;
      uMaskPartial = aMask[nPaddedBits % 8];
      nCountFull = (nPaddedBits / 8);
      nOffset = ((((ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) *4 ) + 7) / 8) - 1;

      /* printf("logical width bits/bytes = %d/%d:%d, physical width bits/bytes = %d/%d:%d, padded bits = %d, uMaskPartial = 0x%02x, nCountFull = %d, offset %d\n",
        ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth,
        (ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) / 8,
        (ptPMSPage->nPageWidthPixels * ptPMSPage->uOutputDepth) & 0x0F,
        ptPMSPage->nRasterWidthBits,
        (ptPMSPage->nRasterWidthBits / 8),
        ptPMSPage->nRasterWidthBits & 0x0F,
        nPaddedBits,
        uMaskPartial,
        nCountFull,
        nOffset); */

      pDst = (unsigned char*)pTo + nOffset;
      nLinesThisBand = ptPMSPage->atPlane[PMS_BLACK].atBand[nBand].uBandHeight;
      nBytesToWritePerLine *= 4;
      for(y = nLinesThisBand; y > 0 ; y--)
      {
        *pDst&=uMaskPartial;
        for(x = 1; x <= nCountFull; x++) {
          pDst[x] = 0x00;
        }
        pDst+=nBytesToWritePerLine;
      }
    }
#endif
  }/* end of CMYK 1 bpp job handling */

  return cbSizeWritten;
}

/**
 * \brief The one and only function call to the PDF output method.
 *
 * \param ptPMSPage Pointer to PMS page structure that contains the complete page.
 * \return PMS_ePDF_Errors error code.
 */
int PDF_PageHandler( PMS_TyPage *ptPMSPage )
{
  PMS_ePDF_Errors eResult = PDF_NoError;
  unsigned int nBytesPerLine;
  unsigned int uBytesToWrite;
  unsigned int uBands;
  unsigned int uBandHeight;
  unsigned int uFirstColorant;
  unsigned int cbBandSize;
  unsigned int j;
  char *pWriteBuf;
  int nCopyNo;
  int nWritten;
#ifdef ADD_PDF_COMMENTS
  unsigned char *p;
#endif
#ifndef NOCOMPRESS
  unsigned int uLastTotal;
  int nResult;
#endif
#ifdef COMPRESS_TEST
  char *pTestCompressedFull;
  char *pTestCompPos;
  char *pTestUncompressedFull;
  char *pTestUncompPos;
  struct z_stream_s zstateUncompress;
#endif

  /* checks for features not supported */
  if((ptPMSPage->uOutputDepth != 1)
    && (ptPMSPage->uOutputDepth != 2)
    && (ptPMSPage->uOutputDepth != 4)
    && (ptPMSPage->uOutputDepth != 8)
    && (ptPMSPage->uOutputDepth != 16))
  {
    PMS_SHOW_ERROR("%d bpp is not yet supported in PDF out.\n", ptPMSPage->uOutputDepth);
    return FALSE;
  }
  /* checks for features not supported */
  if((ptPMSPage->uTotalPlanes == 3) && (ptPMSPage->uOutputDepth < 8))
  {
    PMS_SHOW_ERROR("1, 2 and 4 bpp RGB is not yet supported in PDF out.\n");
    return FALSE;
  }

   /* This assumes each colorant has the same number of bands. */
  uBands = 0;
  uBandHeight = 0;
  uFirstColorant = 0;
  for(j=0; j < PMS_MAX_PLANES_COUNT; j++)
  {
    if(ptPMSPage->atPlane[j].uBandTotal > 0)
    {
      uBands = ptPMSPage->atPlane[j].uBandTotal;
      uBandHeight = ptPMSPage->atPlane[j].atBand[0].uBandHeight;
      uFirstColorant = j;
      break;
    }
  }
  if(uBandHeight==0)
  {
    PMS_SHOW_ERROR("PDF_PageHandler: Band height is zero.\n");
    return PDF_Error_Memory;
  }

  /* dataWidth (bits per scanline including padding) is a multiple of 32, so divide by 8 to get bytes */
  nBytesPerLine = (ptPMSPage->nRasterWidthBits >> 3);

  /* Output several times. */
  /* \todo Perf - just copy the file, we don't need to recreate the PDF every time */
  for(nCopyNo = 1; nCopyNo <= ptPMSPage->nCopies; nCopyNo++)
  {
    /* Bytes per line needed to store pixel interleaved packed raster band.
       Note: The amount of memory could be reduced for bit depths less than 8. */
    gtPDFOut.cbRasterBuffer = ((ptPMSPage->nRasterWidthBits >> 3) * ptPMSPage->uTotalPlanes) * uBandHeight;

    /* \todo Make the raster buffer persistent, and only reallocate if a larger one is needed */
    /* \todo Even better - output bands of raster rather store the whole page (it shouldn't be too tricky to do) */
    gtPDFOut.pRasterBuffer = (char *)OSMalloc(gtPDFOut.cbRasterBuffer,PMS_MemoryPoolPMS);
    if(!gtPDFOut.pRasterBuffer)
    {
      PMS_SHOW_ERROR("PDF_PageHandler: Failed to allocate %d bytes of memory for packed raster line. WidthBits=%d, Planes=%d, BandHeight=%d\n",
        gtPDFOut.cbRasterBuffer, ptPMSPage->nRasterWidthBits, ptPMSPage->uTotalPlanes, uBandHeight);
      return PDF_Error_Memory;
    }

    /* Compression buffer size.
       Note: zlib worst case expansion is an overhead of five bytes per 16KB (about 0.03%)
             plus size bytes.
       Allocate, band size + 1% + 32 bytes */
    gtPDFOut.cbCompressBuffer = (((gtPDFOut.cbRasterBuffer * 100) / 99) + 32);
    /* \todo Make the compression buffer persistent, and only reallocate if a larger one is needed */
    gtPDFOut.pCompressBuffer = (char *)OSMalloc(gtPDFOut.cbCompressBuffer,PMS_MemoryPoolPMS);
    if(!gtPDFOut.pCompressBuffer)
    {
      PMS_SHOW_ERROR("PDF_PageHandler: Failed to allocate %d bytes of memory for compression buffer\n",
        gtPDFOut.cbCompressBuffer);
      OSFree(gtPDFOut.pRasterBuffer, PMS_MemoryPoolPMS);
      gtPDFOut.pRasterBuffer = NULL;
      return PDF_Error_Memory;
    }

    /* write pdf header */
    eResult = PDF_WriteFileHeader(ptPMSPage);
    if(eResult!=PDF_NoError)
    {
      PMS_SHOW_ERROR("PDF_PageHandler: Write header failed.\n");
      eResult = PDF_Error_FileIO;
      PDF_CloseOutput();
      break;
    }

#ifndef NOCOMPRESS
    /* initialise compressor */
    gtPDFOut.zstate.zalloc = &zlib_alloc ;
    gtPDFOut.zstate.zfree = &zlib_free ;
    gtPDFOut.zstate.opaque = NULL ;

    if ( deflateInit(&gtPDFOut.zstate, Z_BEST_SPEED) != Z_OK )
    {
      PMS_SHOW_ERROR("PDF_PageHandler: Compression failed.\n");
      eResult = PDF_Error_FileIO;
      PDF_CloseOutput();
      break;
    }
    uLastTotal = 0;

#ifdef COMPRESS_TEST
    pTestCompressedFull = malloc((ptPMSPage->nRasterWidthBits/8) * ptPMSPage->nPageHeightPixels * 4);
    pTestUncompressedFull = malloc((ptPMSPage->nRasterWidthBits/8) * ptPMSPage->nPageHeightPixels * 4);
    pTestCompPos = pTestCompressedFull;
    pTestUncompPos = pTestUncompressedFull;
#endif
#endif

    for(j=0; j < uBands; j++)
    {
      /* If the band height increases then we need a larger buffer */
      PMS_ASSERT(ptPMSPage->atPlane[uFirstColorant].atBand[j].uBandHeight <= uBandHeight, ("Band Height increased... RasterBuffer and Compress need to be reallocated.\n"));

      /* pixel interleave */
      cbBandSize = PDF_PixelInterleave(ptPMSPage, j, gtPDFOut.pRasterBuffer);

#ifdef ADD_PDF_COMMENTS
      for(p = (unsigned char *)gtPDFOut.pRasterBuffer; p < ((unsigned char *)(gtPDFOut.pRasterBuffer + cbBandSize)); p++) {
        gtPDFOut.uPageChecksum += *p;
      }
#endif

#ifndef NOCOMPRESS
      /* compress */
      gtPDFOut.zstate.avail_in = cbBandSize;
      gtPDFOut.zstate.next_in = (Bytef*)gtPDFOut.pRasterBuffer;
      gtPDFOut.zstate.avail_out = gtPDFOut.cbCompressBuffer;
      gtPDFOut.zstate.next_out = (Bytef*)gtPDFOut.pCompressBuffer;

      /* flush compressor */
      nResult = deflate(&gtPDFOut.zstate, Z_SYNC_FLUSH);
      if(gtPDFOut.zstate.avail_in!=0)
      {
        PMS_SHOW_ERROR("*** ASSERT *** Raster data not entirely consumed by compressor... gtPDFOut.zstate\n" \
               "  .avail_in = %u\n" \
               "  .avail_out = %u\n" \
               "  .total_in = %lu\n" \
               "  .total_out = %lu\n" \
               "  cbBandSize = %u\n",
            gtPDFOut.zstate.avail_in,
            gtPDFOut.zstate.avail_out,
            gtPDFOut.zstate.total_in,
            gtPDFOut.zstate.total_out,
            cbBandSize
          );
      }
      if ( nResult != Z_OK && nResult != Z_STREAM_END )
      {
        PMS_SHOW_ERROR("PDF_PageHandler: Compression no flush failed.\n");
        eResult = PDF_Error_FileIO;
        PDF_CloseOutput();
        break;
      }

      uBytesToWrite = gtPDFOut.zstate.total_out - uLastTotal;
#else
      uBytesToWrite = cbBandSize;
#endif

      if(uBytesToWrite)
      {
#ifndef NOCOMPRESS
        pWriteBuf = gtPDFOut.pCompressBuffer;
        uLastTotal = gtPDFOut.zstate.total_out;
#ifdef COMPRESS_TEST
        memcpy(pTestCompPos, gtPDFOut.pCompressBuffer, uBytesToWrite);
        pTestCompPos += uBytesToWrite;
#endif
#else
        pWriteBuf = gtPDFOut.pRasterBuffer;
#endif
        /* write */
        nWritten = PDF_WriteOutput(pWriteBuf, uBytesToWrite);
        if(nWritten!=(int)uBytesToWrite)
        {
          PMS_SHOW_ERROR("PDF_PageHandler: Write output failed.\n");
          eResult = PDF_Error_FileIO;
          PDF_CloseOutput();
          deflateEnd(&gtPDFOut.zstate);
          return eResult;
        }
        gtPDFOut.filepos += nWritten ;
      }
    }

#ifndef NOCOMPRESS
    /* finish compressor */
    if(uBands>0)
    {
      /* finish compression */
      do {
        gtPDFOut.zstate.avail_in = 0;
        gtPDFOut.zstate.next_in = (Bytef*)NULL;
        gtPDFOut.zstate.avail_out = gtPDFOut.cbCompressBuffer;
        gtPDFOut.zstate.next_out = (Bytef*)gtPDFOut.pCompressBuffer;
        nResult = deflate(&gtPDFOut.zstate, Z_FINISH) ;
        if(gtPDFOut.zstate.avail_in!=0)
        {
          PMS_SHOW_ERROR("*** ASSERT *** Raster data not entirely consumed by compressor... Z_FINISH gtPDFOut.zstate\n" \
                 "  .avail_in = %u\n" \
                 "  .avail_out = %u\n" \
                 "  .total_in = %lu\n" \
                 "  .total_out = %lu\n",
              gtPDFOut.zstate.avail_in,
              gtPDFOut.zstate.avail_out,
              gtPDFOut.zstate.total_in,
              gtPDFOut.zstate.total_out
            );
        }
        if ( nResult != Z_OK && nResult != Z_STREAM_END )
        {
          PMS_SHOW_ERROR("PDF_PageHandler: Compression finish failed.\n");
        }
        uBytesToWrite = gtPDFOut.zstate.total_out - uLastTotal;

        if(uBytesToWrite)
        {
          uLastTotal = gtPDFOut.zstate.total_out;
          nWritten = PDF_WriteOutput(gtPDFOut.pCompressBuffer, uBytesToWrite);
          if(nWritten!=(int)uBytesToWrite)
          {
            eResult = PDF_Error_FileIO;
            PDF_CloseOutput();
            deflateEnd(&gtPDFOut.zstate);
            return eResult;
          }
          gtPDFOut.filepos += nWritten;
#ifdef COMPRESS_TEST
          memcpy(pTestCompPos, gtPDFOut.pCompressBuffer, uBytesToWrite);
          pTestCompPos += uBytesToWrite;
#endif
        }
      }while(nResult == Z_OK);
    }

    /* end deflate */
    if ( deflateEnd(&gtPDFOut.zstate) != Z_OK )
    {
      PMS_SHOW_ERROR("PDF_PageHandler: Compression end failed.\n");
      eResult = PDF_Error_FileIO;
      PDF_CloseOutput();
      return eResult;
    }
#endif

    /* write pdf trailer */
    PDF_WriteFileTrailer(ptPMSPage);

    /* close */
    PDF_CloseOutput();

#ifndef NOCOMPRESS
#ifdef COMPRESS_TEST
    memset(&zstateUncompress, 0x00, sizeof(zstateUncompress));
    zstateUncompress.zalloc = &zlib_alloc;
    zstateUncompress.zfree = &zlib_free;
    if ( inflateInit(&zstateUncompress) != Z_OK )
    {
      PMS_SHOW_ERROR("PDF_PageHandler: Test uncompress failed.\n");
      free(pTestCompressedFull);
      pTestCompressedFull = NULL;
      free(pTestUncompressedFull);
      pTestUncompressedFull = NULL;
    }
    else
    {
      zstateUncompress.avail_in = (uInt)(pTestCompPos - pTestCompressedFull);
      zstateUncompress.avail_out = (ptPMSPage->nRasterWidthBits/8) * ptPMSPage->nPageHeightPixels * 4;
      zstateUncompress.next_in = (Bytef *)pTestCompressedFull;
      zstateUncompress.next_out = (Bytef *)pTestUncompressedFull;
      if (inflate(&zstateUncompress, Z_FINISH) != Z_STREAM_END)
      {
        PMS_SHOW_ERROR("PDF_PageHandler: inflate failed.\n");
      }
      else
      {
        PMS_SHOW("PDF_PageHandler: Compress test... inflate succeeded.\n");
      }

      if (inflateEnd(&zstateUncompress) != Z_OK)
      {
        PMS_SHOW_ERROR("PDF_PageHandler: inflateEnd failed.\n");
      }

      free(pTestCompressedFull);
      pTestCompressedFull = NULL;
      free(pTestUncompressedFull);
      pTestUncompressedFull = NULL;
    }
#endif
#endif
  }
#ifdef DIRECTVIEWPDFTIFF
  if((eResult == PDF_NoError) && (g_tSystemInfo.eOutputType == PMS_PDF_VIEW) && (!g_bBackChannelPageOutput))
  {
    char pBuf[300];

    sprintf(pBuf,"\"start \"hello\" \"%s\"\"",gtPDFOut.tWriteFileOut.szFilename);
    system(pBuf);
  }
#endif
  return eResult;
}

/**
 * \brief Memory allocation routine to be used by the zlib library.
 *
 * \param opaque Unused.
 * \param items Number of elements.
 * \param size Size of each element.
 * \return Address if successful, Z_NULL otherwise.
 */
static void *zlib_alloc(void *opaque, uInt items, uInt size)
{
  void *address ;

  UNUSED_PARAM(void *, opaque) ;

  if ( (address = OSMalloc(items * size,PMS_MemoryPoolPMS)) == NULL )
    return Z_NULL ;

  return address ;
}

/**
 * \brief Memory allocation routine to be used by the zlib library.
 *
 * \param opaque Unused.
 * \param address Pointer to memory to free.
 */
static void zlib_free(void *opaque, void *address)
{
  UNUSED_PARAM(void *, opaque) ;

  OSFree(address,PMS_MemoryPoolPMS) ;
}

