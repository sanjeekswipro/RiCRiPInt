/** \file
 * \ingroup testsrc
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfImageComparer.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief PdfImage comparison functions.
 *
 */
#ifndef _pdfImageComparer_h_
#define _pdfImageComparer_h_

#include "pdfImage.h"

typedef struct {
  long totalPixels;
  int maxSampleDiff;
  double percentageDifference;
} Difference;

/**
 * Returns true if the passed two images can be compared; differences in number
 * of components or bits per component make further comparison pointless.
 */
Bool pdfImagesComparable(PdfImage* a, PdfImage* b);

/**
 * Compare the passed two images. Compatibility should first be checked with
 * pdfImagesComparable().
 */
Difference pdfImagesCompare(PdfImage* a, PdfImage* b, char* maskFile,
                            int tolerance);

#endif

/* Log stripped */

