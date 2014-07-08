/* Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWpdfspool!src:pdfspool.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#include "mem.h"
#include "pdfparse.h"

#include "spooler.h"
#include "spooldev.h"
#include "pdfspool.h"


/* PDF spool context */
struct PDFSPOOL {
  SPOOLER*         spooler;       /* PDF spool file */
  PDF_PARSE*       pdfparser;     /* PDF spool parser */
  PdfSpoolFreeFn * pMemFreeFn;    /* Deallocator */
};


/* Create a new PDF spooler. */
PDFSPOOL* pdfspool_new(PdfSpoolAllocFn * pMemAllocFn, PdfSpoolFreeFn * pMemFreeFn)
{
  PDFSPOOL*   pdfspool;
  SPOOLER_IF  spooler_if;

  /* Allocate a new PDF spooler */
  pdfspool = pMemAllocFn(sizeof(PDFSPOOL));
  if ( pdfspool == NULL ) {
    return(NULL);
  }
  pdfspool->spooler = NULL;
  pdfspool->pdfparser = NULL;
  pdfspool->pMemFreeFn = pMemFreeFn;

  /* Create data spooler */
  pdfspool->spooler = spooler_new();
  if ( pdfspool->spooler == NULL ) {
    pdfspool_end(pdfspool);
    return(NULL);
  }

  /* Set up the spooler interface */
  spooler_if.private_data = pdfspool->spooler;
  spooler_if.append = spooler_append;
  spooler_if.pdflen = spooler_set_pdf_length;
  spooler_if.alloc = pMemAllocFn;
  spooler_if.free = pMemFreeFn;

  /* Create PDF parser */
  pdfspool->pdfparser = pdfparse_new(&spooler_if);
  if ( pdfspool->pdfparser == NULL ) {
    pdfspool_end(pdfspool);
    return(NULL);
  }

  return(pdfspool);

} /* pdfspool_new */


/* Process more data when PDF spooling. */
int32 pdfspool_pdf(
  PDFSPOOL* pdfspool,
  uint8*    buffer,
  int32     length)
{
  /* Keep passing stream data to PDF parser until it says done all it is gonna do */
  /* Either reached end of PDF of problems spooling (out of space) */

  switch ( pdfparse(pdfspool->pdfparser, buffer, length) ) {
  default:
    /* FALLTHROUGH */
  case PDFPARSE_SPOOL_ERROR:
  case PDFPARSE_PARSE_ERROR:
    /* TODO Sort out errors */
    return(SPOOL_ERROR);

  case PDFPARSE_NOT_PDF:
    /* Buffer does not contain start of a PDF job */
    return(SPOOL_NOTPDF);

  case PDFPARSE_MORE_DATA:
    /* Keep reading in data */
    return(SPOOL_MORE);

  case PDFPARSE_EOJ:
    /* Found PDF EOJ */
    return(SPOOL_EOJ);
  }

  /* NEVER REACHED */

} /* pdfspool_pdf */


 /* See if the PDF parser has seen a possible end of data */
int32 pdfspool_eof(
  PDFSPOOL*   pdfspool)
{
  /* Tell the PDF parser there is no more input. */
  pdfparse_complete(pdfspool->pdfparser);

  return((pdfparse_eof(pdfspool->pdfparser) == PDFPARSE_EOJ)
         ? SPOOL_EOJ
         : SPOOL_MORE);

} /* pdfspool_eof */


/* Set spooled data to source PDF job data from */
HqBool pdfspool_use_pdf(
  PDFSPOOL* pdfspool)
{
  spooldev_set_spoolfile(pdfspool->spooler);
  return(spooler_open_pdf(pdfspool->spooler));

} /* pdfspool_use_pdf */


/* Reset spooled data to start of data after the PDF job data */
HqBool pdfspool_use_excess(
  PDFSPOOL* pdfspool)
{
  return(spooler_open_excess(pdfspool->spooler));

} /* pdfspool_use_excess */


/* Return size of spooled PDF data */
Hq32x2 pdfspool_pdf_bytes(
  PDFSPOOL* pdfspool)
{
  return(spooler_get_pdf_length(pdfspool->spooler));
} /* pdfspool_pdf_bytes */


/* Return size of spooled excess data (data beyond end of PDF data) */
Hq32x2 pdfspool_excess_bytes(
  PDFSPOOL* pdfspool)
{
  return(spooler_get_over_read(pdfspool->spooler));
} /* pdfspool_pdf_bytes */


/* Read spooled data. */
int32 pdfspool_read(
  PDFSPOOL* pdfspool,
  uint8*    buff,
  int32     len)
{
  return(spooler_read_excess(pdfspool->spooler, buff, len));

} /* pdfspool_read */


/* Free off the PDF spooler. */
void pdfspool_end(
  PDFSPOOL*   pdfspool)
{
  PdfSpoolFreeFn * pMemFreeFn = pdfspool->pMemFreeFn;

  if ( pdfspool->pdfparser != NULL ) {
    pdfparse_end(pdfspool->pdfparser);
    pdfspool->pdfparser = NULL;
  }

  if ( pdfspool->spooler != NULL ) {
    spooler_end(pdfspool->spooler);
    pdfspool->spooler = NULL;
  }

  pMemFreeFn(pdfspool);

} /* pdfspool_end */

/* eof */
