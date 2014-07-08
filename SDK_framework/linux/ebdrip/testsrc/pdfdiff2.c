/** \file
 * \ingroup testsrc
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfdiff2.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#include "pdfImage.h"
#include "pdfImageWriter.h"
#include "pdfImageComparer.h"
#include <stdlib.h>


/**
 * Main entry point of the pdfdiff2 application.
 * Note that all errors are reported via the error() function, which reports the
 * error then exits. Thus there are no error returns from any function.
 */
int main(int argc, char *argv[])
{
  PdfImage a, b;
  Difference difference;
  Bool success = TRUE;
  int tolerance = 0;

  if ( argv[1][0] == '-' && argv[1][1] == 't' ) {
    tolerance = atoi(argv[2]);
    argc -= 2; argv += 2;
  }
  if (argc != 4) {
    error("Usage: pdfdiff2 [-t <tolerance>] 1.pdf 2.pdf mask.pdf\n\n"
          "If successful, returns three numbers, separated by whitespace:\n"
          "  total percentage difference\n"
          "  total differing pixels\n"
          "  max sample difference\n", NULL);
  }

  pdfImageInit(&a, argv[1]);
  pdfImageInit(&b, argv[2]);

  if (! pdfImagesComparable(&a, &b)) {
    success = FALSE;
  } else {
    difference = pdfImagesCompare(&a, &b, argv[3], tolerance);
    printf("%f %ld %d", difference.percentageDifference, difference.totalPixels,
           difference.maxSampleDiff);
  }

  pdfImageDispose(&a);
  pdfImageDispose(&b);

  if (success) {
    return 0;
  }
  else {
    error("Images are not comparable.", NULL);
    /* error() doesn't return, this is here to please the compiler. */
    return 1;
  }
}

/* Log stripped */

