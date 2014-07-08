/** \file
 * \ingroup pdfdiff
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfImageWriter.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Methods for generating a PDF wrapped raster.
 */
#ifndef _pdfImageWriter_h_
#define _pdfImageWriter_h_

#include <stdio.h>
#include "zlib.h"
#include "pdfUtil.h"

#define XREF_MAX_SIZE 20

typedef struct {
  FILE* file;
  int objectIndex;
  int offsets[XREF_MAX_SIZE];
} XRefBuilder;

#define PDF_WRITER_IO_BUFFER_SIZE 1024 * 10

typedef struct {
  char* fileName;
  FILE* file;
  int width, height;
  XRefBuilder xref;
  long streamStart;

  z_stream z;
  uint8 outputBuffer[PDF_WRITER_IO_BUFFER_SIZE];
  uint8 deflatedBuffer[PDF_WRITER_IO_BUFFER_SIZE];
  uint8* writePoint;
} PdfWriter;


/**
 * Initialise the passed writer. Once initialised, image data should be written
 * by calls to pdfWriterDeflate(). After all data has been written, call
 * pdfWriterFinish().
 */
void pdfWriterInit(PdfWriter* self, char* fileName,
                   int width, int height,
                   int bitsPerComponent, char* colorSpace);

/**
 * Write the passed image data.
 */
void pdfWriterDeflate(PdfWriter* self, uint8* data, size_t length);

/**
 * Finish writing the pdf file and close it.
 */
void pdfWriterFinish(PdfWriter* self, float mediaBox[4]);

#endif

/* Log stripped */

