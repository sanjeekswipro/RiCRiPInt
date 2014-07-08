/* Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pcl6rast.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * PCL6 backend functions
 */

/**
 * \file
 * \ingroup OIL
 *
 * \brief A simple, unoptimized, implementation of PCL6 output.
 *
 * Note that currently, the incoming raster must be RGB, Monochrome contone. or
 * Monochrome halftone.
 *
 */

/* Target requirements :
 *   N-up         : This is an existing RIP feature, not a backend function.
 *   Booklet      : Not in first revision. Can probably implement simply with
 *                   @PJL SET BOOKLET=ON
 *   Scaling      : This needs some thought, but we can certainly do it.
 *                   Currently 1:1
 *   Duplex       : Yes, done.
 *   Copy Settings: As in number of copies? If so, done.
 *   Poster       : This splits large images across multiple pages. Not currently
 *                   supported.
 *   Print Order  : That would be a RIP function. Might be tricky.
 *   Paper Size   : Can do.
 *   Paper Source : Later, we don't currently have this in our properties dialog,
 *                   but not difficult.
 *   Paper Type   : Obtained from Print Ticket, otherwise, Plain.
 *   Resolution   : Done.
 *   Orientation  : This needs to be checked carefully with the real printer,
 *                   when we have a working driver. Best to say 'later'.
 *   Watermark / Overlay  :
 *                  These would normally be applied by the RIP. The user specifies
 *                  some text in the printer properties dialog, and the RIP chooses
 *                  a fontsize to fit the page, and applies it to the image at the
 *                  end of rendering. Needs adding to the GPD, and supporting in the
 *                  RIP. We have the technology...
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "oil_pcl6rast.h"
#include "hqnstrutils.h"  /* for strlen_int32 */
#include "skinkit.h"      /* for SkinMonitorf() */
#include "oil_pclcommon.h"

#include "oil_malloc.h"

/*#define CAPTUREPCL*/
/* \brief Output compression mode. It can be one of
 *   eNoCompression
 *   eRLECompression
 *   eJPEGCompression      (Unsupported)
 *   eDeltaRowCompression
 */
static const uint32 fCompressMode = eDeltaRowCompression;

/*
 * \brief PCL6 Raster description.
 *
 * Note on margin handling. We can :
 * Clip the top of the raster for an unprintable area (nClipTop).
 * Clip the unprintable bottom of the raster (nLastLine).
 * Clip RGB pixels from the left edge of each line (cbClipLeft)
 * In this example, we'll assume that the incoming raster is exactly
 * the same size as the media, and that the printer has 1/6 inch
 * unprintable area at each edge, and discard that much image from all
 * sides.
 * Recent SDKs offer user-defined settings for the margins, which are
 * not used here.
*/

typedef struct _PCL6_RASTER
{
  uint32    nBitsPixel;      /* Bits per pixel. */
  uint32    nColorants;      /* Colorants per pixel. */
  uint32    nCompressMode;   /* eNoCompression, eRLECompression, etc. */
  uint8   * pCompBuf;        /* Will usually hold the whole compressed page. */
  uint8   * pNext;           /* Points into pCompBuf. */
  uint32    cbCompBuffer;    /* Size of pCompBuf */
  uint32    nTopLine;        /* May not be the same as the band's top line index, it we're clipping */
  uint32    bAligned;        /* Data in multiples of 4-bytes */
  uint32    cbLineBytes;     /* Bytes per line, including any rounding */
  uint32    imageWidthPix;   /* Bytes per line, excluding any rounding */
  uint32    cbLineWidthInBytes;
  uint32    cbRasterIncr;    /* Byte count of incoming raster lines */
  uint32    bLandscape;      /* pMSE's dimensions should be reversed */
  uint32    dataWidth;       /* We don't get the rasterdata in every call */
  int32     nClipTop;        /* Lines to discard */
  int32     cbClipLeft;      /* BYTES of RGB, NOT pixels! */
  int32     nLastLine;       /* When we're clipping the bottom */
  uint8   * pSeedRow;
  MEDIASIZEENTRY * pMSE;     /* Media size lookup table. */
} PCL6_RASTER;

/**
 * \brief The file name format string when outputting to a file.
 * ".pcl" is appended to the fully-qualified path.
 */
uint8 *fileformatPCL6 = (uint8*)"%f.pcl";
#ifdef CAPTUREPCL
FILE *pPrintFile;
#endif

static PCL6_RASTER_handle * pcl6NewRasterHandle (uint8 * ps_filename);
static int32  pcl6NewHeader (PCL6_RASTER_handle * handle, RasterDescription * pRD);
static int32  pcl6EndPageSequence (PCL6_RASTER_handle *handle);
static int32  pcl6EndJobSequence (PCL6_RASTER_handle *handle);

static int32  thisIsFirstPage = 1;

/**
 * \brief Standard clRip RASTER_START callback function to handle
 * the start of a page.
 *
 * If the job has multiple pages, we should already have an opened raster
 * handle. Otherwise we create a raster handle suitable for clRip or PrintPipeline.
 *
 * \param ps_filename A formatted PostScript pathlist/filename, based upon the input
 * job name and the \c fileformatPCL6 string supplied in this file.
 *
 * \param pRD [in] Describes the properties of the current page's raster image.
 *
 * \param handlep [in/out] This parameter is supplied to all of this device's
 * callbacks.
 *
 * \return RASTER_noErr on success, or else one of the other RASTER_xxxx
 * values defined in rasthand.h
 *
 */
int32 PCL6_RASTER_start(uint8 *ps_filename, RasterDescription *pRD, RASTER_handle *handlep)
{
  int32      result = RASTER_noErr;
  uint32     cbCompBuff = 0;    /* Size of pCompressBuff */
  uint32     cb1Line;         /* Length of 1 line in bytes, before compression. */

  PCL6_RASTER_handle * handle;
  PCL6_RASTER * phPcl = NULL;
#ifdef WIN32
  DOC_INFO_1 DocInfo;
  char PrinterName[128];
  DWORD cbPrinterName = 128;
  char DataType[] = "RAW";
  BOOL bresult;
#endif
  /* Check job's raster format. */
  if (getRasterFormat (pRD) == RF_UNKNOWN)
    return RASTER_requestErr;

  if (*handlep == (RASTER_handle) NULL)
  {
    thisIsFirstPage = 1;
    if ( (handle = pcl6NewRasterHandle(ps_filename)) == NULL )
      return RASTER_requestErr;
#ifdef WIN32	  
    bresult = GetDefaultPrinter(PrinterName, &cbPrinterName);
    if(bresult == 0)
    {
      result = RASTER_requestErr;
      goto PCL6_raster_start_error;
    }
    bresult = OpenPrinter(PrinterName, &(handle->fd_out), NULL);
    if(bresult == 0)
    {
      result = RASTER_requestErr;
      goto PCL6_raster_start_error;
    }
    DocInfo.pDocName = (LPSTR)ps_filename;
    DocInfo.pOutputFile = NULL;
    DocInfo.pDatatype = DataType;
    result = StartDocPrinter(handle->fd_out, 1, (LPBYTE)(&DocInfo));
    if ( result == 0 ) {
      result = RASTER_requestErr;
      goto PCL6_raster_start_error;
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
      goto PCL6_raster_start_error;
    }
#endif
#ifdef CAPTUREPCL
    pPrintFile = fopen("c:\\ebdwrapperout.prn", "wb");
#endif
  }
  else
  {
    thisIsFirstPage = 0;
    handle = (PCL6_RASTER_handle *) * handlep;
  }

  phPcl = (PCL6_RASTER *) handle->pPrivate;

  if (thisIsFirstPage) {
    memset (phPcl, 0, sizeof(PCL6_RASTER));
  }
  else {
    phPcl->nTopLine = 0;  /* Reset the output line index counter */
  }
  phPcl->dataWidth = pRD->dataWidth;
  phPcl->nBitsPixel = pRD->rasterDepth;

  /* Perform some validation of the incoming job: */
  if (1 != pRD->nSeparations) {
    result = RASTER_requestErr;
    goto PCL6_raster_start_error;
  }
  if (3 == pRD->numChannels) {
    phPcl->nColorants = 3;
    if (8 != pRD->rasterDepth) {
      result = RASTER_requestErr;
      goto PCL6_raster_start_error;
    }
  }
  else if (1 == pRD->numChannels) {
    /* support grayscale and halftones */
    phPcl->nColorants = 1;
  }
  else {
    result = RASTER_requestErr;
    goto PCL6_raster_start_error;
  }

  /* The compression can be implemented as a PrintTicket entry, if so required. */
  /* Note that compression may be turned 'off' partway through a page. */
  phPcl->nCompressMode = fCompressMode;
  /* We compress whole bands */
  handle->fPcl6BlockModeCompression = TRUE;
  phPcl->bAligned = TRUE;

  /*
   * Allocate a [new] compression buffer, if necessary.
   * (We'll try to send the entire page in one go, for speed.)
   */
  cb1Line = pRD->dataWidth / 8;
  cbCompBuff = (pRD->imageHeight + 1) * cb1Line;
  cbCompBuff += 16384;  /* allow for per-block preamble, lead-in, lead-out */

  if (phPcl->cbCompBuffer < cbCompBuff) {
    if (phPcl->pCompBuf)
      OIL_free (OILMemoryPoolJob, phPcl->pCompBuf);

	phPcl->pCompBuf = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, cbCompBuff);
	if (NULL == phPcl->pCompBuf) {
      cbCompBuff = 64 * 1024 * 1024; /* A more reasonable value, perhaps.. */
      phPcl->pCompBuf = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, cbCompBuff);
      if (NULL == phPcl->pCompBuf) {
        cbCompBuff = 32 * 1024 * 1024; /* Last attempt.. */
        phPcl->pCompBuf = OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock, cbCompBuff);
        if (NULL == phPcl->pCompBuf) {
          /* Cannot alloc enough memory - report error */
          result = RASTER_requestErr;
          goto PCL6_raster_start_error;
        }
      }
    }
    phPcl->cbCompBuffer = cbCompBuff;
    phPcl->pNext = phPcl->pCompBuf;
  }

  phPcl->pMSE = pclPerformMediaLookup (pRD, &phPcl->bLandscape);
  if (! phPcl->pMSE)
  {
    result = RASTER_requestErr;
    goto PCL6_raster_start_error;
  }
  /* Setup PCL6 : create a header in the compression buffer ==> pcl6StartPage. */
  if (! pcl6NewHeader(handle, pRD)) {
    result = RASTER_requestErr;
    goto PCL6_raster_start_error;
  }

PCL6_raster_start_error:
  if ( result != RASTER_noErr ) {
    /* Error happened - clean up */
    (void) PCL6_RASTER_job_end((RASTER_handle)&handle);
  }

  *handlep = (RASTER_handle) handle;

  return result;
}

/**
 * \param handle
 * \return The number of bytes available for use in the compression buffer.
 */
static uint32 getCompBufferRemaining (PCL6_RASTER_handle* handle)
{
  PCL6_RASTER* phPcl = (PCL6_RASTER *) handle->pPrivate;
  uint32 cbBytesUsed = CAST_PTRDIFFT_TO_UINT32 (phPcl->pNext - phPcl->pCompBuf);
  assert (cbBytesUsed <= phPcl->cbCompBuffer);
  return phPcl->cbCompBuffer - cbBytesUsed;
}

/**
 * \brief Flush the compression buffer to disk.
 *
 * \param handle
 * \return \c TRUE on success, \c FALSE otherwise.
 */

static int32 flushCompBuffer (PCL6_RASTER_handle* handle)
{
  PCL6_RASTER* phPcl = (PCL6_RASTER *) handle->pPrivate;
  uint32 cbBytesUsed = CAST_PTRDIFFT_TO_UINT32 (phPcl->pNext - phPcl->pCompBuf);
#ifdef WIN32
  DWORD cWritten;
  BOOL bresult=0;
#endif

  if (cbBytesUsed > 0)
  {
#ifdef WIN32
    bresult = WritePrinter(handle->fd_out, phPcl->pCompBuf, cbBytesUsed, &cWritten);
    if(bresult == 0)
      return FALSE;
#else
    fwrite(phPcl->pCompBuf, 1, cbBytesUsed, handle->fd_out);
#endif
  }
#ifdef CAPTUREPCL
  fwrite(phPcl->pCompBuf, 1, cbBytesUsed, pPrintFile);
#endif
  phPcl->pNext = phPcl->pCompBuf;
  return TRUE;
}


/**
 * \brief Ensure enough space is available in the compression buffer
 * to store \c nRequired bytes.
 *
 * \param handle
 * \param nRequired
 * \return \c TRUE on success, \c FALSE otherwise.
 */
static int32 reserveCompBufferSpace (PCL6_RASTER_handle* handle, uint32 nRequired)
{
  if (getCompBufferRemaining (handle) < nRequired)
    return flushCompBuffer (handle);

  return TRUE;
}

/**
 * \brief Multiple calls are made to this function for each page.
 * The OEM might apply late screening here, and wrap the raster
 * data in printer commands, or they might pass the raw image data
 * to one or more filters in the PrintPipeline for further processing.
 * It is not possible for the OEM to reliably set the number of raster
 * lines delivered to this function, they should write their code
 * allowing for the lines value to be any number greater than 0.
 * In practice, it's currently always 1 line per band, but this may
 * change when performance issues are addressed. The last band
 * may then contain less lines than the others.
 *
 * This example very crudely converts the RGB contone data to dithered
 * monochrome with a simplistic 'threshold' algorithm. OEMs are
 * expected to implement their own screening, or license it from
 * GGS or a third party.
 *
 * \param rHandle This is the handlep value we assigned in the
 * PCL6_RASTER_start function.
 *
 * \param pBand data Points at the first byte of the current band of
 * raster image data.
 *
 * \param topline The 0-based index of the first raster image line in
 * the current band.
 *
 * \param nLinesThisTime The number of lines of image data in the current
 * band. The line count in the last band of a page may be different
 * from all of the preceding ones.
 *
 * \param nLineWidth This is the length, in bytes, of each line of
 * image within the band, including any padding. The actual imageable
 * portion of the line was derived earlier from
 * pRasterDescription->imageWidth.
 *
 * \param channel We ignore this value, as we're using RGB data in
 * pixel-interleaved format, which is considered as a single channel.
 *
 * \return RASTER_noErr on success, or else one of the other RASTER_xxxx
 * values defined in rasthand.h
 */
int32 PCL6_RASTER_write_data(RASTER_handle rHandle, void  *pBand, int32 topline,
                             int32 nLinesThisTime, int32 nLineWidth, int32 channel)
{
  PCL6_RASTER_handle *handle = (PCL6_RASTER_handle*) rHandle;
  PCL6_RASTER * phPcl = (PCL6_RASTER *) handle->pPrivate;
  int32   result = RASTER_noErr;
  uint8 * pHdr;
  uint8 * pRasterLine = NULL;        /* ptr to start of line in band buf */
  uint8   nAlignment;
  int32   nCurrentLine = 0;   /* line within band */
  uint32  cbCompressed = 0;
  uint8 * pcbSend;
  uint32  cbData;
  uint32  cbFree;
  uint8 * pSeed;
  UNUSED_PARAM(int32, channel);



  /* Do nothing if we're in the bottom margin */
  if (phPcl->nTopLine > (uint32)phPcl->nLastLine)
    return RASTER_noErr;

  /* Discard any unprintable top and bottom margin */
  while (topline < phPcl->nClipTop) {
    ++topline;
    ++nCurrentLine;
    --nLinesThisTime;

    if (0 == nLinesThisTime)
      return RASTER_noErr;     /* no more data and not out of top clip yet */
  }

  if (phPcl->nLastLine < (int)(nLinesThisTime + phPcl->nTopLine))
    nLinesThisTime = phPcl->nLastLine - phPcl->nTopLine;
  if (0 == nLinesThisTime)
    return RASTER_noErr;

  phPcl->cbLineWidthInBytes = phPcl->imageWidthPix * phPcl->nColorants;
  if (phPcl->nBitsPixel == 1)
    phPcl->cbLineWidthInBytes = (phPcl->cbLineWidthInBytes + 7) / 8;

  if (NULL == phPcl->pSeedRow)
  {
    /* Free and NULL phPcl->pSeedRow at the end of the page. */
    phPcl->pSeedRow =  OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock,
                                            phPcl->cbLineBytes+4);
      /* PP added 2-2-2010 as the seed is not initialized; not sure if this is correct for all cases!!!!!!*/
      memset(phPcl->pSeedRow,'\0',phPcl->cbLineWidthInBytes+4);
  /* ..and we should free and NULL it at the end of the job. */
  }

  /*
   * There must be enough room for the whole band and its header in the buffer,
   * otherwise we can't set the compressed size value after compression. Otherwise,
   * we must send uncompressed data.
   * The header size (ReadImage + its attrs, EmbeddedData + its length) is 24 bytes.
   */
  {
    uint32 nRequired = 512 + (nLinesThisTime * phPcl->cbLineBytes);
    if (! reserveCompBufferSpace (handle, nRequired))
      return RASTER_fileOutputErr;
    if (getCompBufferRemaining (handle) < nRequired)
    {
      fprintf (stderr, "Warning: Using uncompressed PCL output buffer.\n");
      phPcl->nCompressMode = eNoCompression;
    }
  }

  /* Mono contones need inverting such that 0 = Black, 255 = White */
  if ((1 == phPcl->nColorants) && (8 == phPcl->nBitsPixel))
  {
    invertMonoContoneData ((uint8*) pBand + nCurrentLine * phPcl->cbRasterIncr,
                           phPcl->cbRasterIncr * nLinesThisTime);
  }

  pHdr = phPcl->pNext;

  /* Prepare the header, and append the raw data
   *
   * The line within the source image at which this block is
   * located. The line position is in source pixels. The line must
   * be in sequence relative to the end line of the previous
   * ReadImage. The first start line must be zero.
   */
  PUTBYTE(pHdr, UInt16DataType);
  PUTLEWORD(pHdr, phPcl->nTopLine);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eStartLine);

  /*
  * The height of this image data block in source pixels.
  */
  PUTBYTE(pHdr, UInt16DataType);
  PUTLEWORD(pHdr, (uint16)nLinesThisTime);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eBlockHeight);

  /*
   * Indicate the compression mode for the block being sent.
   */
  PUTBYTE(pHdr, UByteDataType);
  PUTBYTE(pHdr, (uint8)phPcl->nCompressMode);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eCompressMode);

  /*
   * If we're getting whole bands from the RIP, then the input lines
   * are all 4-byte aligned. See RasterCallback() in skintest.c
   */
  nAlignment = (handle->fPcl6BlockModeCompression) ? 4 : 1;
  if (4 == nAlignment) {
    assert (0 == (phPcl->cbLineBytes & 0x03));
  }
  PUTBYTE(pHdr, UByteDataType);
  PUTBYTE(pHdr, nAlignment);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, ePadBytesMultiple);

  PUTBYTE(pHdr, eReadImage);

  assert ((nLineWidth > 0) && (nLinesThisTime > 0));

  PUTBYTE(pHdr, eEmbeddedData);
  pcbSend = pHdr;
  cbCompressed = nLineWidth * nLinesThisTime;
  PUT4BYTE(pHdr, cbCompressed);  /* This is a placeholder. */

  /* Compress the raster. */
  cbCompressed = 0;
  cbFree = getCompBufferRemaining (handle);
  pSeed = phPcl->pSeedRow;

  while (nLinesThisTime--)
  {
    pRasterLine = (uint8 *)pBand + (nCurrentLine * phPcl->cbRasterIncr) + phPcl->cbClipLeft;

    switch (phPcl->nCompressMode)
    {
    case eNoCompression:
      cbData = phPcl->cbLineBytes;
      memcpy (pHdr, pRasterLine, cbData);
      break;

    case eRLECompression:
      cbData = pclCompressPackbits(pHdr, pRasterLine, phPcl->cbLineBytes);
      break;

    /* CAUTION : DeltaRow is unlikely to be supported by the printer
     * when compressing more than 1 line at a time.
     */
    case eDeltaRowCompression:
      cbData = pclCompressDeltaRow(pHdr, pSeed, pRasterLine,
                  phPcl->bAligned ? phPcl->cbLineBytes : phPcl->cbLineWidthInBytes, TRUE);
      pSeed = pRasterLine;
      break;

    default:
      assert(FALSE); /* Unsupported Compression mode */
      return RASTER_noErr; /* discard */
    }

    ++phPcl->nTopLine;
    ++nCurrentLine;
    assert (cbFree >= cbData);
    pHdr += cbData;
    cbCompressed += cbData;
    cbFree -= cbData;
  }
  phPcl->pNext = pHdr;


  /* Set the real (possibly-compressed) data length of this band. */
  assert (cbCompressed);

  PUT4BYTE(pcbSend, cbCompressed);

  if (phPcl->pSeedRow) {
    assert (pRasterLine);
    memcpy(phPcl->pSeedRow, pRasterLine, phPcl->cbLineBytes);
  }
  /* We only flush when necessary, or at EOP */
  return result;
}


/**
 * \brief This function is called once at the end of each page in each job.
 * As we don't know whether an EOJ should follow this page, we'll wait until
 * our next call from the RIP, which should be either
 * begin a new page (when we'll send EOP and close the pipe) or,
 * the EOJ call (when we'll send EOP +EOJ before closing the pipe).
 * This function is therefore a no-op.
 *
 * \param handlep This is the handlep value we assigned in our
 * PCL6_RASTER_start function.
 *
 * \param result This should be whatever value was returned by the
 * last call to PCL6_RASTER_write_data.
 *
 * \return RASTER_noErr on success, or else one of the other RASTER_xxxx
 * values defined in rasthand.h
 */
int32 PCL6_RASTER_finish(RASTER_handle *handlep, int32 result)
{
  PCL6_RASTER_handle *handle = (PCL6_RASTER_handle *) *handlep;
  PCL6_RASTER * phPcl = (PCL6_RASTER *) handle->pPrivate;
  int32 nEnd = RASTER_noErr;

  if (NULL == phPcl->pCompBuf)
    return result;

  if ((nEnd = pcl6EndPageSequence(handle)) != RASTER_noErr)
    result = RASTER_fileOutputErr;
  if (phPcl->pSeedRow) {
    OIL_free (OILMemoryPoolJob, phPcl->pSeedRow);
    phPcl->pSeedRow = NULL;
  }

  return result;
}


/**
 * \brief This function is called after the last page has been closed.
 *
 * \param handlep This is the handlep value we assigned in our
 * PCL6_RASTER_start function.
 *
 * \return RASTER_noErr on success, or else one of the other RASTER_xxxx
 * values defined in rasthand.h
 */
int32 PCL6_RASTER_job_end(RASTER_handle *handlep)
{
  PCL6_RASTER_handle * handle = (PCL6_RASTER_handle*) * handlep;

  if (handle)
  {
    PCL6_RASTER * phPcl = (PCL6_RASTER *) handle->pPrivate;
    int32 result = RASTER_noErr;

    if (phPcl && phPcl->pCompBuf)
    {
      /* Don't leave it on, next job may be for a different device! */
      handle->fPcl6BlockModeCompression = FALSE;
      if ((result = pcl6EndJobSequence(handle)) != RASTER_noErr)
        return result;
#ifdef WIN32
      EndDocPrinter(handle->fd_out);
      ClosePrinter(handle->fd_out);
      handle->fd_out = (HANDLE)-1;
#else
      fclose(handle->fd_out);
      handle->fd_out = -1;
      system("lpr -r printready.prn");
#endif

      OIL_free (OILMemoryPoolJob, phPcl->pCompBuf);
      phPcl->pCompBuf = NULL;
      phPcl->cbCompBuffer = 0;
      OIL_free (OILMemoryPoolJob, handle);
      handle = NULL;
    }
    *handlep = (RASTER_handle*)NULL;
  }
  return RASTER_noErr;
}

/**
 * \brief Creates a new pcl6 raster handle.
 *
 * \param ps_filename the PS name including the output PS device.
 * \return the new handle if successful; NULL otherwise.
 */
static PCL6_RASTER_handle * pcl6NewRasterHandle (uint8 * ps_filename)
{
  PCL6_RASTER_handle * handle = NULL;
  UNUSED_PARAM(uint8 *, ps_filename);

#ifdef WIN32
  handle = (PCL6_RASTER_handle*) OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock,
                                            sizeof(PCL6_RASTER_handle) +
                                            sizeof(PCL6_RASTER));
  if (handle != NULL)
  {
    handle->fd_out = (HANDLE)-1;
    handle->pPrivate = (uint8 *)(handle + 1);
  }
#else
  handle = (PCL6_RASTER_handle *) OIL_malloc(OILMemoryPoolJob, OIL_MemNonBlock,
                                            sizeof(PCL6_RASTER_handle));
  if (handle != NULL)
  {
    handle->fd_out = -1;
    strcpy((char *)handle->filename,"printready.prn");
  }
#endif
  return handle;
}

/**
 * \brief Write the job header.
 */
static void writeJobStartHeader (char** pHdr, PCL6_RASTER * phPcl, RasterDescription * pRD)
{
  *pHdr = writeJobPJL (*pHdr, pRD, phPcl->pMSE, 6);

  /********* PCL6 mode. Append the preamble *********/
  sprintf(*pHdr, ") HP-PCL XL;3;0" kCRLF);   /* Little-endian and protocol v3.0 */
  *pHdr += strlen(*pHdr);

  /* Set measurement units = our raster resolutions in dpi */
  PUTBYTE(*pHdr, UInt16XyType);
  PUTLEWORD(*pHdr, (uint16)pRD->xResolution);
  PUTLEWORD(*pHdr, (uint16)pRD->yResolution);
  PUTBYTE(*pHdr, ByteAttribId);
  PUTBYTE(*pHdr, eUnitsPerMeasure);

  PUTBYTE(*pHdr, UByteDataType);
  PUTBYTE(*pHdr, eInch);
  PUTBYTE(*pHdr, ByteAttribId);
  PUTBYTE(*pHdr, eMeasure);

  /* Set error handling method */
  PUTBYTE(*pHdr, UByteDataType);
  PUTBYTE(*pHdr, eErrorPage);
  PUTBYTE(*pHdr, ByteAttribId);
  PUTBYTE(*pHdr, eErrorReport);
  /* Position 1 */
  PUTBYTE(*pHdr, eBeginSession);
}

/**
 * \brief Constructs a PCL/PJL header block for the page.
 *
 * NB: Most printers will ignore any PJL commands which they don't understand.
 * The ones used here are typical. Specific printers may require additional
 * commands, or different command parameters.
 * Some printers require the Configure Raster Data command, not the Simple Colour one.
 * That mode locks-out "ESC*r#u", Simple Color and "ESC*v#W[data]", and Configure
 * Image Data. It is not used in this simple example.
 *
 * Some parameters are obtained from the PrintTicket.
 *
 * \param handle PCL6_Raster_handle.
 * \param pRD RasterDescription struct supplied by the RIP.
 * \return 0 on failure, otherwise the header length in bytes.
 */
static int32 pcl6NewHeader (PCL6_RASTER_handle * handle, RasterDescription * pRD)
{
  PCL6_RASTER * phPcl = (PCL6_RASTER *) handle->pPrivate;
  uint32 imageWidthPix;   /* actual pixels to send after clipping */
  uint32 imageHeightPix;  /* actual lines to send after clipping */

  char  *pHdr;
  int32  nLen;
  uint8  *pData = NULL;
  float  nfValX, nfValY;
#ifdef WIN32
  BOOL bresult;
#endif 

  if ((NULL == pRD) || (NULL == phPcl))
    return 0;  /* Fatal error, but fail gracefully. */

  /* In block mode, we're asking for the *padded* bands */
  phPcl->cbRasterIncr = pRD->dataWidth / 8;
  imageWidthPix = pRD->imageWidth;
  imageHeightPix = pRD->imageHeight;

  /* We use fixed unprintable margins here in this example.
   * A release quality driver might do something more sophisticated,
   * in particular, it might get the RIP to supply a clipped raster,
   * in which case our media lookup would need replacing or modifying.
   */
  phPcl->nLastLine = (int32)(phPcl->pMSE->nfHeight * (float)pRD->yResolution);
  if (imageHeightPix > (uint32)phPcl->nLastLine)
    imageHeightPix = phPcl->nLastLine;  /* Clip to media */
  phPcl->nLastLine -= (int32)(pRD->yResolution / 3.33); /* Actual number of raster lines to send */

  /* Set top/left margin clipping. */
  if (imageHeightPix > (uint32) phPcl->nLastLine)
  {
    /* Clip equally top and bottom */
    phPcl->nClipTop = (int32) (imageHeightPix - phPcl->nLastLine) / 2;
  }
  else
  {
    phPcl->nClipTop = 0;
  }
  phPcl->cbClipLeft = (int32)(pRD->xResolution/6.0);  /* Pixels */

  imageWidthPix -= (phPcl->cbClipLeft + phPcl->cbClipLeft);
  imageHeightPix -= (phPcl->nClipTop + phPcl->nClipTop);
  phPcl->cbClipLeft *= phPcl->nColorants;
  if ((1 == phPcl->nBitsPixel) && (1 == phPcl->nColorants))
    phPcl->cbClipLeft = (phPcl->cbClipLeft + 7) / 8;  /* Adjust for 1bpp */

  /* 4-byte align the output data if required */
  imageWidthPix &= (8 == phPcl->nBitsPixel) ? 0xFFFFFFFC : 0xFFFFFFE0;
  phPcl->cbLineBytes = imageWidthPix * phPcl->nColorants;
  if (1 == phPcl->nBitsPixel)
    phPcl->cbLineBytes /= 8;

  /* Reset unit  */
  if (! reserveCompBufferSpace (handle, 512))
    return 0;
  pHdr = (char *)phPcl->pNext;

#ifdef WIN32
  bresult = StartPagePrinter(handle->fd_out);
  if(bresult == 0)
    return 0;
#endif
  /* Write job header. */
  if(thisIsFirstPage)
    writeJobStartHeader (&pHdr, phPcl, pRD);

  /* DataSourceType seems to be required, though none were ever defined */
  PUTBYTE(pHdr, UByteDataType);
  PUTBYTE(pHdr, eDefaultDataSource);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eSourceType);

  /* Confirm big/small-endian */
  PUTBYTE(pHdr, UByteDataType);
  PUTBYTE(pHdr, eBinaryLowByteFirst);  /* Intel/AMD hardware */
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eDataOrg);
  /* Position 2 */
  PUTBYTE(pHdr, eOpenDataSource);

  /* The paper tray can be implemented as a PrintTicket entry, if so required. */
  PUTBYTE(pHdr, UByteDataType);
  PUTBYTE(pHdr, eAutoSelect);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eMediaSource);

  /* The orientation can be implemented as a PrintTicket entry, if so required. */
  PUTBYTE(pHdr, UByteDataType);
  PUTBYTE(pHdr, ((phPcl->bLandscape) ? eLandscapeOrientation : ePortraitOrientation));
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eOrientation);

  /* We derive papertype from the PrintTicket, doing nothing if it's unset.
   * NB : The manual states that Plain paper is represented as the null string, not "Plain"
   * HP's own 4650 driver does send Plain.
   */
  pData = (uint8*)pRD->PtfMediaType;
  nLen = strlen_int32((char *)pData);
  if (0 != nLen) {
    uint8 *pEnd;
    if (strcmp((char *)pData, "Plain")) {
      pEnd = pData + nLen;
      PUTBYTE(pHdr, UByteArrayType);
      PUTBYTE(pHdr, UInt16DataType);
      PUTLEWORD(pHdr, (uint16)nLen);
      while (pData < pEnd) {
        PUTBYTE(pHdr, *(pData++));
      }
      PUTBYTE(pHdr, ByteAttribId);
      PUTBYTE(pHdr, eMediaType);
    }
  }

  if (phPcl->pMSE->szName[0]) {
    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, (char)phPcl->pMSE->nPclEnum);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eMediaSize);
  }
  else {
    /* Set a custom media size in inches  ENDIAN DEPENDANT */
    nfValX = (float)pRD->imageWidth  / (float)pRD->xResolution;
    nfValY = (float)pRD->imageHeight / (float)pRD->yResolution;
    PUTBYTE(pHdr, Real32XyType);
    PUT4BYTE(pHdr, *(uint32*)&nfValX);
    PUT4BYTE(pHdr, *(uint32*)&nfValY);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eCustomMediaSize);

    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, eInch);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eCustomMediaSizeUnits);
  }

  /* Position 3 */
  PUTBYTE(pHdr, eBeginPage);
  /* Position 4 */
  PUTBYTE(pHdr, eSetPageDefaultCTM);

  /* We check if "borderless" is set and if yes, set the origin to
   * topleft corner. Otherwise, we use some safe defaults, 1/6 inch,
   * calculated from the resolution values, in this example.
   */
  PUTBYTE(pHdr, SInt16XyType);
  if (0 == strcmp("Borderless", (const char *)pRD->PtfPageBorderless)) {
    PUTLEWORD(pHdr, 0);
    PUTLEWORD(pHdr, 0);
  }
  else {
    PUTLEWORD(pHdr, (uint16)(pRD->xResolution/6.0));
    PUTLEWORD(pHdr, (uint16)(pRD->yResolution/6.0));
  }
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, ePageOrigin);
  /* Position 5 */
  PUTBYTE(pHdr, eSetPageOrigin);

  /* The "scale to fit" can be implemented as a PrintTicket entry, if so required. */
  nfValX = nfValY = 1.0F;
  PUTBYTE(pHdr, Real32XyType);
  PUT4BYTE(pHdr, *(uint32*)&nfValX);
  PUT4BYTE(pHdr, *(uint32*)&nfValY);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, ePageScale);
  /* Position 6 */
  PUTBYTE(pHdr, eSetPageScale);

  PUTBYTE(pHdr, UByteDataType);
  if (3 == phPcl->nColorants) {
    PUTBYTE(pHdr, eRGB);
  }
  else {
    PUTBYTE(pHdr, eGray);  /* Bilevel rasters use a grayscale palette */
  }
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eColorSpace);

  /* Position 7 */
  PUTBYTE(pHdr, eSetColorSpace);

  /* The halftoning style and bit depth can be implemented as a PrintTicket entry,
   * if so required.
   */

  /* If RGB, we let the printer screen it */
  if (3 == phPcl->nColorants) {
    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, eDeviceBest);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eDeviceMatrix);
    /* Position 8 */
    PUTBYTE(pHdr, eSetHalftoneMethod);
  }

  /* We probably don't need these, but turn transparency off, just in case. */
  PUTBYTE(pHdr, UByteDataType);
  PUTBYTE(pHdr, eOpaque);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eTxMode);
  /* Position 9 */
  PUTBYTE(pHdr, eSetPaintTxMode);

  PUTBYTE(pHdr, UByteDataType);
  PUTBYTE(pHdr, eOpaque);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eTxMode);
  /* Position 10 */
  PUTBYTE(pHdr, eSetSourceTxMode);

  /*
   * Note that 0,0 is interpreted relative to the page origin, which
   * is specified earlier.
   */
  PUTBYTE(pHdr, SInt16XyType);
  PUTLEWORD(pHdr, 0);
  PUTLEWORD(pHdr, 0);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, ePoint);
  /* Position 11 */
  PUTBYTE(pHdr, eSetCursor);

  if (1 == phPcl->nColorants) {
    /* Single colorant uses gray colorspace. */
    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, eGray);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eColorSpace);

    if (1 == phPcl->nBitsPixel) {
      /* Create a grayscale palette for monochrome output. */
      PUTBYTE(pHdr, UByteDataType);
      PUTBYTE(pHdr, e8Bit);
      PUTBYTE(pHdr, ByteAttribId);
      PUTBYTE(pHdr, ePaletteDepth);

      PUTBYTE(pHdr, UByteArrayType);
      PUTBYTE(pHdr, UInt16DataType);
      PUTLEWORD(pHdr, 2);
      PUTBYTE(pHdr, 255);
      PUTBYTE(pHdr,   0);
      PUTBYTE(pHdr, ByteAttribId);
      PUTBYTE(pHdr, ePaletteData);
    }

    /* Position 12 */
    PUTBYTE(pHdr, eSetColorSpace);
  }
  else if ((8 == phPcl->nBitsPixel) && (3 == phPcl->nColorants)) {
    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, eRGB);          /* or eSRGB */
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eColorSpace);
    /* Position 12 */
    PUTBYTE(pHdr, eSetColorSpace);
  }
  else {
    assert(FALSE); /* We only support Gray and 8-bit RGB. */
    return 0;
  }

  /*
   * ColorMapping : Specifies whether the component color
   * mapping is direct or indexed through a palette.
   */
  if (1 == phPcl->nBitsPixel) {
    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, eIndexedPixel);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eColorMapping);
  }
  else {
    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, eDirectPixel);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eColorMapping);
  }
  /*
   * ColorDepth The number of bits per image component
   * in our raster (not the print engine, see eDitherMatrixDepth,
   * above).  1, 4, or 8.
   */
  if (8 == phPcl->nBitsPixel) {
    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, e8Bit);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eColorDepth);
  }
  else {
    PUTBYTE(pHdr, UByteDataType);
    PUTBYTE(pHdr, e1Bit);
    PUTBYTE(pHdr, ByteAttribId);
    PUTBYTE(pHdr, eColorDepth);
  }
  /* The width of the image source in source pixels.
   * We're doing any necessary clipping, so this MUST
   * be the length of each line we send (ignoring any padding)
   */
  PUTBYTE(pHdr, UInt16DataType);
  PUTLEWORD(pHdr, (uint16)imageWidthPix);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eSourceWidth);
  phPcl->imageWidthPix = imageWidthPix;

  /*
   * The height of the image source in source pixels.
   */
  PUTBYTE(pHdr, UInt16DataType);
  PUTLEWORD(pHdr, (uint16)imageHeightPix);
  PUTBYTE(pHdr, ByteAttribId);
  PUTBYTE(pHdr, eSourceHeight);

  /*
  * The size of the destination box on the page for the image in
  * "current user units".
  */
  PUTBYTE(pHdr, UInt16XyType);
  PUTLEWORD(pHdr, (uint16)imageWidthPix);    /* Raster height in pixels */
  PUTLEWORD(pHdr, (uint16)imageHeightPix);  /* Raster height in pixels */
  PUTBYTE(pHdr, ByteAttribId);
  /* Position 13 */
  PUTBYTE(pHdr, eDestinationSize);

  /* Position 14 */
  PUTBYTE(pHdr, eBeginImage);          /* Finally... */

  /* Return data length */
  nLen = (int)(pHdr - (char *)phPcl->pNext);
  phPcl->pNext = (uint8*)pHdr;
  return nLen;
}

static int32 pcl6EndPageSequence (PCL6_RASTER_handle *handle )
{
  PCL6_RASTER * phPcl = (PCL6_RASTER *) handle->pPrivate;
  char * pHdr;
#ifdef WIN32  
  BOOL bresult;
#endif 

  if ((NULL == phPcl->pCompBuf) || (0 >= phPcl->cbCompBuffer))
    return RASTER_noErr;

  if (! reserveCompBufferSpace (handle, 8))
    return RASTER_fileOutputErr;
  pHdr = (char *)phPcl->pNext;

  PUTBYTE(pHdr, eEndImage);
  PUTBYTE(pHdr, eEndPage);
  PUTBYTE(pHdr, eCloseDataSource);

  phPcl->pNext = (uint8*)pHdr;

  flushCompBuffer (handle);
#ifdef WIN32  
  bresult = EndPagePrinter(handle->fd_out);
  if(bresult == 0)
    return RASTER_fileOutputErr;
#endif
  return RASTER_noErr;
}

/**
 * All PCL XL sessions on HP LaserJet Printer devices must
 * be explicitly invoked through a PJL ENTER LANGUAGE command.
 * There may be only one currently executing PCL XL
 * imaging session in PCL LaserJet Printers. Following the
 * EndSession in the stream, the PCLXL session must end with a
 * UEL.
 * Example:
   \verbatim
   A PCL XL Job that will consist of a single, 3 page session:
   <esc>%-12345X
   @PJL ENTER LANGUAGE = PCLXL <CR><LF>
   ...<PCL XL stream header>...
   ... begin session ...
   ... page 1 ...
   ... page 2 ...
   ... page 3 ...
   ... end session ...
   <esc>%-12345X
   @endverbatim
 * NOTE that 'jobs' which contain a mixture of page sizes,
 * orientations, resolutions, etc. *may* succeed if the PJL
 * header is compatible with them - which seems unlikely,
 * as a lot of the info is sent in both PCL and PJL.
 *
 * \param handle PCL6_Raster_handle.
 * \return RASTER_noErr if successful, otherwise RASTER_xxxErr code.
 */
static int32 pcl6EndJobSequence (PCL6_RASTER_handle *handle)
{
  PCL6_RASTER * phPcl = (PCL6_RASTER *) handle->pPrivate;
  char * pHdr;

  if ((NULL == phPcl->pCompBuf) || (0 >= phPcl->cbCompBuffer))
    return RASTER_noErr;

  if (! reserveCompBufferSpace (handle, 64))
    return RASTER_fileOutputErr;
  pHdr = (char *)phPcl->pNext;

  PUTBYTE(pHdr, eEndSession);

  sprintf (pHdr, "%c%c-12345X", 0x1B, 0x25);
  pHdr += strlen(pHdr);

  sprintf (pHdr, "@PJL EOJ." kCRLF);
  pHdr += strlen(pHdr);

  sprintf (pHdr, "%c%c-12345X", 0x1B, 0x25);
  pHdr += strlen(pHdr);
  assert (getCompBufferRemaining (handle) > 0);

  phPcl->pNext = (uint8*)pHdr;
  if (! flushCompBuffer (handle))
    return RASTER_fileOutputErr;

  return RASTER_noErr;
}


