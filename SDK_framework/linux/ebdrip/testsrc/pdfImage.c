/** \file
 * \ingroup pdfdiff
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfImage.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#include <stdlib.h>
#include <string.h>

#include "pdfImage.h"


/**
 * Read the colorspace details from 'in', and return the number of components in
 * the space. The token '/ColorSpace' should have been the last thing read from
 * 'in'. Indirect objects are not supported.
 *
 * @return If the color space was parsed successfully, the number of components
 *         is returned, otherwise 0 is returned.
 */
static int parseColorSpace(Scanner* in, Bool *additive, Bool mustfind)
{
  char* name = scanString(in, TRUE);

  *additive = FALSE;

  if (name != NULL && strcmp(name, "[") == 0)
    name = scanString(in, mustfind);

  if (strcmp(name, "/DeviceGray") == 0) {
    *additive = TRUE;
    return 1;
  }
  if (strcmp(name, "/Separation") == 0) {
    return 1;
  }
  if (strcmp(name, "/DeviceRGB") == 0) {
    *additive = TRUE;
    return 3;
  }
  if (strcmp(name, "/DeviceCMYK") == 0) {
    return 4;
  }
  if (strcmp(name, "/Lab") == 0) {
    return 3;
  }
  if (strcmp(name, "/DeviceN") == 0) {
    /* For deviceN, count the number of strings in the colorants array. */
    char* string = scanString(in, mustfind);
    if (string != NULL && strcmp(string, "[") == 0) {
      int total = 0;
      /* Give up looking for the array end after finding a lot of components;
       * something must have gone wrong. */
      while (total < 50) {
        string = scanString(in, mustfind);
        if (string != NULL && strcmp(name, "]") == 0) {
          return total;
        }
        else {
          total ++;
        }
      }
    }
    return 0;
  }

  return 0;
}

/**
 * Parse a decode array from 'in', and return the number of components to be
 * decoded.  The token '/Decode' should have been the last thing read from
 * 'in'. Indirect objects are not supported.
 *
 * @return If the decode array was parsed successfully, the number of components
 *         is returned, otherwise 0 is returned.
 */
static int parseDecode(Scanner* in, Bool mustfind)
{
  char* string = scanString(in, mustfind);
  int count = 0;

  if (string != NULL && strcmp(string, "[") == 0) {
    string = scanString(in, mustfind);
    while (string != NULL && strcmp(string, "]") != 0) {
      count ++;
      string = scanString(in, mustfind);
    }
  }
  return count / 2;
}

/**
 * Initialise the passed Inflator.
 */
void inflatorInit(InflateState* self, char* fileName, FILE* file)
{
  z_stream* z = &self->z;

  self->fileName = fileName;
  self->file = file;
  self->bitsLeft = 0;

  self->inputBuffer = malloc(IO_BUFFER_SIZE);
  self->outputBuffer = malloc(IO_BUFFER_SIZE);
  if (self->inputBuffer == NULL || self->outputBuffer == NULL) {
    error("Failed to allocate inflation buffers.", NULL);
  }

  z->zalloc = Z_NULL;
  z->zfree = Z_NULL;
  z->opaque = NULL;

  z->next_in = Z_NULL;
  z->avail_in = 0;
  z->next_out = self->outputBuffer;
  z->avail_out = IO_BUFFER_SIZE;

  if (inflateInit(z) != Z_OK)
    error("Failed to initialise inflater.", NULL);
}

void inflatorDispose(InflateState* self)
{
  free(self->inputBuffer);
  free(self->outputBuffer);
}

/**
 * Fill the zlib input buffer with data read from the file.
 */
void fillBuffer(InflateState* self)
{
  self->z.next_in = self->inputBuffer;
  self->z.avail_in = (uInt)fread(self->inputBuffer, 1, IO_BUFFER_SIZE, self->file);
  if (self->z.avail_in == 0)
    error("Premature end of compressed data in file:", self->fileName);
}

/**
 * Return the next inflated byte from the image input stream. The client should
 * not read more data than is present in the image.
 */
uint8 inflateByte(InflateState* self)
{
  z_stream* z = &self->z;
  int zres = Z_OK;
  uint8 byte;

  if (z->avail_out == IO_BUFFER_SIZE) {
    /* No inflated data left - read from disk if necessary and inflate. */
    if (z->avail_in == 0) {
      fillBuffer(self);
    }

    z->next_out = self->readPoint = self->outputBuffer;
    zres = inflate(z, Z_SYNC_FLUSH);
    if (zres != Z_OK && zres != Z_STREAM_END) {
      error("Problem decompressing data in file:", self->fileName);
    }
    if (z->avail_out == IO_BUFFER_SIZE) {
      error("Inflate produced no data. File:", self->fileName);
    }
  }

  byte = *self->readPoint;
  self->readPoint ++;
  z->avail_out ++;

  return byte;
}

/**
 * Return the total number of bytes read by the inflator.
 */
int inflatorGetTotalBytesRead(InflateState* self)
{
  return self->z.total_in;
}

/* See header for doc. */
void pdfImageInit(PdfImage* self, char* fileName)
{
  int foundCount = 0;
  Scanner in;
  int numComponents = 0;
  Bool additive = FALSE;

  self->fileName = fileName;
  self->file = fopen(fileName, "rb");
  if (self->file == NULL)
    error("Failed to read file:", fileName);

  self->numComponents = 0; self->additive = FALSE;
  scanInit(&in, self->file);

  while (foundCount < 4) {
    char* name = scanName(&in, TRUE);
    if (name == NULL) {
      error("Failed to read image parameters.", NULL);
    }

    if (strcmp(name, "/Width") == 0) {
      foundCount ++;
      self->width = scanInt(&in, TRUE);
    }
    else if (strcmp(name, "/Height") == 0) {
      foundCount ++;
      self->height = scanInt(&in, TRUE);
    }
    else if (strcmp(name, "/BitsPerComponent") == 0) {
      foundCount ++;
      self->bitsPerComponent = scanInt(&in, TRUE);
      self->inflator.mask = (1 << self->bitsPerComponent) - 1;
    }
    else if (self->numComponents == 0) {
      /* We can figure out the number of components from either the color space
       * or the decode array; either one may be indirect and thus unreadable. */
      if (strcmp(name, "/ColorSpace") == 0) {
        self->numComponents = parseColorSpace(&in, &additive, TRUE);
        if (self->numComponents > 0) {
          foundCount ++;
          self->additive = additive;
        }
      }
      else if (strcmp(name, "/Decode") == 0) {
        self->numComponents = parseDecode(&in, TRUE);
        if (self->numComponents > 0) {
          foundCount ++;
        }
      }
    }
  }

  /* Find the start of the image stream. */
  scanMatch(&in, "stream");
  scanConsumeSpace(&in);
  self->streamStart = ftell(in.file);
  inflatorInit(&self->inflator, self->fileName, self->file);



  /* Look for *any* second image. If found, we can't compare these PDF
     files (for now)! */

  /* Skip binary stream data. */
  scanMatch(&in, "endstream");

  numComponents = 0;
  foundCount = 0;
  while (foundCount < 4) {
    char* name = scanName(&in, FALSE);
    if (name == NULL)
      break;

    if (strcmp(name, "/Width") == 0) {
      foundCount ++;
      (void)scanInt(&in, FALSE);
    }
    else if (strcmp(name, "/Height") == 0) {
      foundCount ++;
      (void)scanInt(&in, FALSE);
    }
    else if (strcmp(name, "/BitsPerComponent") == 0) {
      foundCount ++;
      (void)scanInt(&in, FALSE);
    }
    else if (numComponents == 0) {
      /* We can figure out the number of components from either the
       * color space or the decode array; either one may be indirect
       * and thus unreadable. */
      if (strcmp(name, "/ColorSpace") == 0) {
        numComponents = parseColorSpace(&in, &additive, FALSE);
        if (numComponents > 0)
          foundCount ++;
      } else if (strcmp(name, "/Decode") == 0) {
        numComponents = parseDecode(&in, FALSE);
        if (numComponents > 0)
          foundCount ++;
      }
    }
  }

  if (foundCount == 4) {
    error("Found more than one image in file:", fileName);
  }

  if (fseek(in.file, self->streamStart, SEEK_SET) != 0) {
    error("Failed to seek back to beginning of first image.", NULL);
  }

}

/* See header for doc. */
int pdfImageNextSample(PdfImage* self)
{
  Byte bytes[2];
  InflateState* inflator = &self->inflator;

  switch (self->bitsPerComponent) {
  case 16:
    bytes[0] = inflateByte(inflator);
    bytes[1] = inflateByte(inflator);
    return (bytes[0] << 8) | bytes[1];

  case 8:
    return inflateByte(inflator);

  default:
    if (inflator->bitsLeft == 0) {
      inflator->byte = inflateByte(inflator);
      inflator->bitsLeft = 8;
    }
    inflator->bitsLeft -= self->bitsPerComponent;
    return (inflator->byte >> inflator->bitsLeft) & inflator->mask;
  }
}

/* See header for doc. */
void pdfImageSeekToImageEnd(PdfImage* self)
{
  fseek(self->file,
        self->streamStart + inflatorGetTotalBytesRead(&self->inflator),
        SEEK_SET);
}

/* See header for doc. */
Bool pdfImageReadMediaBox(PdfImage* self, float mediaBox[4], Bool mustfind)
{
  Scanner in;

  scanInit(&in, self->file);
  if (! scanMatch(&in, "/MediaBox"))
    return FALSE;

  scanConsumeSpace(&in);
  if (scanChar(&in) != '[') {
    error("Expected start of media box array.", NULL);
  }

  mediaBox[0] = scanFloat(&in, mustfind);
  mediaBox[1] = scanFloat(&in, mustfind);
  mediaBox[2] = scanFloat(&in, mustfind);
  mediaBox[3] = scanFloat(&in, mustfind);
  return TRUE;
}

/* See header for doc. */
void pdfImageFinishLine(PdfImage* self)
{
  /* Lines are byte-padded. */
  self->inflator.bitsLeft = 0;
}

/* See header for doc. */
void pdfImageDispose(PdfImage* self)
{
  fclose(self->file);
  inflatorDispose(&self->inflator);
}

/* Log stripped */

