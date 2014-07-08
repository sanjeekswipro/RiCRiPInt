/** \file
 * \ingroup testsrc
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfImage.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Methods for reading an image from a wrapper-raster PDF file, as
 * produced by SWprod_coreRip, GGEprod_gg_example or SWplug_pdfraster.
 */
#ifndef _pdfImage_h_
#define _pdfImage_h_

#include <stdio.h>
#include "zlib.h"
#include "pdfUtil.h"

#define IO_BUFFER_SIZE 1024 * 10

/**
 * This structure contains state necessary to inflate deflated image data.
 */
typedef struct {
  FILE* file;
  char* fileName;

  /** zlib decompression state. */
  z_stream z;

  /**
   * A buffer containing compressed data read from disk; IO_BUFFER_SIZE bytes
   * long
   */
  uint8* inputBuffer;

  /** A buffer containing inflated data; IO_BUFFER_SIZE bytes long. */
  uint8* outputBuffer;

  /**
   * Pointer to the next inflated byte within outputBuffer. The amount of
   * inflated data available is tracked within the z_stream state.
   */
  uint8* readPoint;

  /**
   * The current input byte, used when reading data with sample sizes less than
   * 8 bits.
   */
  uint8 byte;

  /* Tracking state for the byte member. */
  int bitsLeft, mask;
} InflateState;

/**
 * This structure represents an image which has been read from a disk-based
 * PDF container.
 */
typedef struct {
  FILE* file;
  char* fileName;
  int width, height;
  int bitsPerComponent;
  int numComponents;
  Bool additive;

  /** File offset to the start of the PDF image stream. */
  long streamStart;

  /** Used to inflate the deflated image data. */
  InflateState inflator;
} PdfImage;

/**
 * Initialise an image from the passed file. The opened file will be at the
 * correct location for reading image data after this method.
 */
void pdfImageInit(PdfImage* self, char* fileName);

/**
 * Scan for the /MediaBox key/value pair starting at the current file position.
 *
 * @return FALSE if the media box could not be found before EOF was reached.
 */
Bool pdfImageReadMediaBox(PdfImage* self, float mediaBox[4], Bool mustfind);

/**
 * Read the next sample from the image.
 */
int pdfImageNextSample(PdfImage* self);

/**
 * Call immediately after a whole line of samples have been read.
 */
void pdfImageFinishLine(PdfImage* self);

/**
 * Seek to the end of the image data in the PDF; this should only be called
 * after the image data has been read.
 */
void pdfImageSeekToImageEnd(PdfImage* self);

/**
 * Dispose the passed image.
 */
void pdfImageDispose(PdfImage* self);

#endif

/* Log stripped */

