/** \file
 * \ingroup pdfdiff
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfImageWriter.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#include <string.h>

#include "pdfImageWriter.h"

void xrefBuilderInit(XRefBuilder* self, FILE* file)
{
  self->file = file;
  self->objectIndex = 0;
}

void xrefBuilderRecordObject(XRefBuilder* self)
{
  self->offsets[self->objectIndex] = ftell(self->file);
  self->objectIndex ++;
}

void xrefBuilderWrite(XRefBuilder* self)
{
  int i;
  fprintf(self->file,
          "xref\n"
          "0 %d\n"
          "0000000000 65535 f\r\n",
          self->objectIndex + 1);
  for (i = 0; i < self->objectIndex; i ++) {
    fprintf(self->file, "%010d 00000 n\r\n", self->offsets[i]);
  }
}

/* See header for doc. */
void pdfWriterInit(PdfWriter* self, char* fileName,
                   int width, int height,
                   int bitsPerComponent, char* colorSpace) {
  self->fileName = fileName;
  self->file = fopen(fileName, "wb");
  if (self->file == NULL) {
    error("Failed to create file:", fileName);
  }
  self->width = width;
  self->height = height;
  xrefBuilderInit(&self->xref, self->file);

  fprintf(self->file, "%%PDF-1.5\n");
  xrefBuilderRecordObject(&self->xref);

  fprintf(self->file,
          "1 0 obj\n"
          "<<\n"
          "  /Type /XObject\n"
          "  /Subtype /Image\n"
          "  /BitsPerComponent %d\n"
          "  /Width %d\n"
          "  /Height %d\n"
          "  /Length 2 0 R\n"
          "  /ColorSpace %s\n"
          "  /Filter /FlateDecode\n"
          ">> stream\n",
          bitsPerComponent, width, height, colorSpace);

  self->streamStart = ftell(self->file);

  /* Initialise deflater. */
  self->z.zalloc = Z_NULL;
  self->z.zfree = Z_NULL;
  self->z.opaque = NULL;

  self->z.next_in = Z_NULL;
  self->z.avail_in = 0;
  self->z.next_out = Z_NULL;
  self->z.avail_out = 0;

  if (deflateInit(&self->z, Z_BEST_SPEED) != Z_OK) {
    error("Failed to initialise deflater.", NULL);
  }
  self->writePoint = self->outputBuffer;
}

/**
 * Deflate buffered data and write it to disk.
 */
static void deflateData(PdfWriter* self, size_t length)
{
  size_t amountToWrite;

  self->z.next_in = self->outputBuffer;
  self->z.avail_in = (uInt)length;

  while (self->z.avail_in > 0) {
    self->z.next_out = self->deflatedBuffer;
    self->z.avail_out = PDF_WRITER_IO_BUFFER_SIZE;

    if (deflate(&self->z, Z_NO_FLUSH) != Z_OK) {
      error("Error deflating image data.", NULL);
    }

    amountToWrite = PDF_WRITER_IO_BUFFER_SIZE - self->z.avail_out;
    if (fwrite(self->deflatedBuffer, 1, amountToWrite, self->file) != amountToWrite) {
      error("Error writing image data.", NULL);
    }
  }
}

/**
 * Deflate any remaining data and write it to disk.
 */
static void deflateFinish(PdfWriter* self)
{
  int amountToWrite;
  int result;

  deflateData(self, self->writePoint - self->outputBuffer);

  do {
    self->z.next_out = self->deflatedBuffer;
    self->z.avail_out = PDF_WRITER_IO_BUFFER_SIZE;

    result = deflate(&self->z, Z_FINISH);
    amountToWrite = PDF_WRITER_IO_BUFFER_SIZE - self->z.avail_out;
    if (fwrite(self->deflatedBuffer, 1, amountToWrite, self->file) != amountToWrite) {
      error("Error writing image data.", NULL);
    }
  } while (result == Z_OK);

  if (result != Z_STREAM_END) {
    error("Problem finishing deflated data.", self->fileName);
  }

  if (deflateEnd(&self->z) != Z_OK) {
    error("Problem in deflateEnd().", NULL);
  }
}

/* See header for doc. */
void pdfWriterDeflate(PdfWriter* self, uint8* data, size_t length)
{
  size_t spaceLeft = PDF_WRITER_IO_BUFFER_SIZE - (self->writePoint - self->outputBuffer);
  while (length > 0) {
    size_t amount = min(length, spaceLeft);
    length = length - amount;

    memcpy(self->writePoint, data, amount);
    data += amount;
    self->writePoint += amount;
    spaceLeft -= amount;
    if (spaceLeft == 0) {
      spaceLeft = PDF_WRITER_IO_BUFFER_SIZE;
      self->writePoint = self->outputBuffer;
      deflateData(self, PDF_WRITER_IO_BUFFER_SIZE);
    }
  }
}

/* See header for doc. */
void pdfWriterFinish(PdfWriter* self, float mediaBox[4])
{
  int streamLength;
  int xRefStart;

  /* Write any remaining data. */
  deflateFinish(self);
  streamLength = ftell(self->file) - self->streamStart;

  fprintf(self->file,
          "\r\n"
          "endstream\n"
          "endobj\n");

  xrefBuilderRecordObject(&self->xref);
  fprintf(self->file,
          "2 0 obj\n"
          "%d\n"
          "endobj\n",
          streamLength);

  /* Page content. */
  {
    char content[1024];
    sprintf(content, "%f 0 0 %f 0 0 cm /Im1 Do", mediaBox[2] - mediaBox[0],
            mediaBox[3] - mediaBox[1]);

    xrefBuilderRecordObject(&self->xref);
    fprintf(self->file,
            "3 0 obj\n"
            "<< /Length %lu >>\n"
            "stream\n"
            "%s\n"
            "endstream\n"
            "endobj\n",
            (unsigned long)strlen(content), content);
  }

  xrefBuilderRecordObject(&self->xref);
  fprintf(self->file,
          "4 0 obj\n"
          "<<\n"
          "  /Type /Page\n"
          "  /Resources <<\n"
          "    /XObject << /Im1 1 0 R >>\n"
          "  >>\n"
          "  /Contents 3 0 R\n"
          "  /Parent 5 0 R\n"
          "  /MediaBox [ %f %f %f %f ]\n"
          ">>\n"
          "endobj\n",
          mediaBox[0], mediaBox[1], mediaBox[2], mediaBox[3]);

  xrefBuilderRecordObject(&self->xref);
  fprintf(self->file,
          "5 0 obj\n"
          "<<\n"
          "  /Type /Pages\n"
          "  /Kids [ 4 0 R ]\n"
          "  /Count 1\n"
          ">>\n"
          "endobj\n");

  xrefBuilderRecordObject(&self->xref);
  fprintf(self->file,
          "6 0 obj\n"
          "<<\n"
          "  /Type /Catalog\n"
          "  /Pages 5 0 R\n"
          ">>\n"
          "endobj\n");

  xRefStart = ftell(self->file);
  xrefBuilderWrite(&self->xref);

  fprintf(self->file,
          "trailer\n"
          "<<\n"
          "  /Size 7\n"
          "  /Root 6 0 R\n"
          ">>\n"
          "startxref\n"
          "%d\n"
          "%%%%EOF\n",
          xRefStart);

  fclose(self->file);
  self->file = NULL;
}

/* Log stripped */

