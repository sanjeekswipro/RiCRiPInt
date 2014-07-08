/** \file
 * \ingroup pdfdiff
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfImageComparer.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#include <stdlib.h>

#include "pdfImageComparer.h"
#include "pdfImageWriter.h"

static char* monoColorSpace = "[\n"
  "    /Indexed\n"
  "    /DeviceGray\n"
  "    1\n"
  "    <\n"
  "      FF\n" /* White */
  "      00\n" /* Black */
  "    >\n"
  "  ]";

static char colorCodedSpace[] =
  "[\n"
  "    /Indexed\n"
  "    /DeviceRGB\n"
  "    6\n"
  "    <\n"
  "      FFFFFF\n" /* White */
  "      FFAF00\n" /* Orange */
  "      00FFFF\n" /* Cyan */
  "      00FF00\n" /* Green */
  "      FF00FF\n" /* Magenta */
  "      FF0000\n" /* Red */
  "      000000\n" /* Black */
  "    >\n"
  "  ]";

/**
 * Returns true if the passed two images can be compared; differences in number
 * of components or bits per component make further comparison pointless.
 */
Bool pdfImagesComparable(PdfImage* a, PdfImage* b)
{
  return a->bitsPerComponent == b->bitsPerComponent &&
         a->numComponents == b->numComponents;
}

/**
 * Consume the specified number of samples.
 */
void consumeSamples(PdfImage* image, int count)
{
  while (count > 0) {
    count --;
    pdfImageNextSample(image);
  }
}

/**
 * Quantise the passed difference value into the appropriate mask palette
 * indices.
 */
int quantise(int difference)
{
  if (difference == 0) {
    return 0;
  }
  else {
    if (difference < 7) {
      if (difference == 1)
        return 1;
      else if (difference < 4)
        return 2;
      else
        return 3;
    }
    else {
      if (difference < 11)
        return 4;
      else if (difference < 25)
        return 5;
      else
        return 6;
    }
  }
}

/* See header for doc. */
Difference pdfImagesCompare(PdfImage* a, PdfImage* b, char* maskFile,
                            int tolerance)
{
  Difference diff = {0};
  int x, y, c;
  int minWidth, minHeight;
  PdfWriter mask;
  uint8* maskLine = NULL;
  uint8* maskWritePoint;
  size_t maskLineSize = 0;
  int bitsPerComponent, initialShift;
  char* colorSpace;
  float mediaBox[4];
  int range = 1 << a->bitsPerComponent;
  Bool opposite;

  minWidth = min(a->width, b->width);
  minHeight = min(a->height, b->height);

  opposite = a->additive != b->additive;

  /* For mono input images we'll produce monochrome masks, otherwise use a 4-bit
   * color-coded mask. */
  if (a->bitsPerComponent == 1) {
    bitsPerComponent = 1;
    initialShift = 7;
    colorSpace = monoColorSpace;
  }
  else {
    bitsPerComponent = 4;
    initialShift = 4;
    colorSpace = colorCodedSpace;
  }

  if (maskFile != NULL) {
    pdfWriterInit(&mask, maskFile, minWidth, minHeight, bitsPerComponent,
                  colorSpace);
    maskLineSize = ((minWidth * bitsPerComponent) + 7) / 8;
    maskLine = malloc(maskLineSize);
    if (maskLine == NULL) {
      error("Failed to allocate mask line.", NULL);
    }
  }

  for (y = 0; y < minHeight; y ++) {
    int outShift = initialShift;
    uint8 byte = 0;

    maskWritePoint = maskLine;
    for (x = 0; x < minWidth; x ++) {
      int maxDiffThisPixel = 0;

      for (c = 0; c < a->numComponents; c ++) {
        int sampleA = pdfImageNextSample(a);
        int sampleB =
          opposite ? range - pdfImageNextSample(b) : pdfImageNextSample(b);
        int d = sampleA - sampleB;

        if (d != 0) {
          if (d < 0) {
            d = 0 - d;
          }
          if (d <= tolerance)
            d = 0;
          if (d > maxDiffThisPixel) {
            maxDiffThisPixel = d;
            if (d > diff.maxSampleDiff) {
              diff.maxSampleDiff = d;
            }
          }
        }
      }

      if (maxDiffThisPixel != 0) {
        diff.totalPixels ++;
      }

      /* Update the mask using the max sample difference for this pixel. */
      if (maskFile != NULL) {
        if (maxDiffThisPixel != 0) {
          if (bitsPerComponent == 1) {
            byte |= (1 << outShift);
          }
          else {
            byte |= (quantise(maxDiffThisPixel) << outShift);
          }
        }
        outShift -= bitsPerComponent;
        if (outShift < 0) {
          *maskWritePoint = byte;
          maskWritePoint ++;
          byte = 0;
          outShift = initialShift;
        }
      }
    }

    /* Write any remaining data. */
    if (outShift != initialShift) {
      *maskWritePoint = byte;
    }

    consumeSamples(a, (a->width - x) * a->numComponents);
    consumeSamples(b, (b->width - x) * b->numComponents);

    pdfImageFinishLine(a);
    pdfImageFinishLine(b);

    if (maskFile != NULL) {
      /* Write the line. */
      pdfWriterDeflate(&mask, maskLine, maskLineSize);
    }
  }

  /* Seek to the end of the image stream; the Page object containing the media
   * box should be after it. */
  pdfImageSeekToImageEnd(a);

  if (! pdfImageReadMediaBox(a, mediaBox, TRUE)) {
    error("Failed to find media box.", NULL);
  }

  if (maskFile != NULL) {
    pdfWriterFinish(&mask, mediaBox);
  }
  diff.percentageDifference = (diff.totalPixels * 100.0) / (minWidth * minHeight);
  return diff;
}

/* Log stripped */

