/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWpdfspool!export:pdfspool.h(EBDSDK_P.1) $
 */

#ifndef __PDFSPOOL_H__
#define __PDFSPOOL_H__  (1)

#include  "std.h"
#include  "swdevice.h"


typedef struct PDFSPOOL PDFSPOOL;

typedef void * (PdfSpoolAllocFn)(size_t cbSize);

typedef void (PdfSpoolFreeFn)(void * pMem);

extern DEVICETYPE PDF_Spooler_Device_Type;

/* Create a PDF spooler */
extern
PDFSPOOL* pdfspool_new(PdfSpoolAllocFn * pMemAllocFn, PdfSpoolFreeFn * pMemFreeFn);

#define SPOOL_ERROR   (1)   /* Error spooling PDF data to disk */
#define SPOOL_NOTPDF  (2)   /* Spool data is not for a PDF job */
#define SPOOL_MORE    (3)   /* PDF end of job not yet detected */
#define SPOOL_EOJ     (4)   /* PDF end of job reached */

/* Spool PDF job data looking for the end of the job */
extern
int32 pdfspool_pdf(
  PDFSPOOL* pdfspool,
  uint8*    data,
  int32     length);

/* No more PDF job data - have we reached the end? */
extern
int32 pdfspool_eof(
  PDFSPOOL* pdfspool);

/* Set spooled data to source PDF job data from */
extern
HqBool pdfspool_use_pdf(
  PDFSPOOL* pdfspool);

/* Reset spooled data to start of data after the PDF job data */
extern
HqBool pdfspool_use_excess(
  PDFSPOOL* pdfspool);

/* Return size of spooled PDF data */
extern
Hq32x2 pdfspool_pdf_bytes(
  PDFSPOOL* pdfspool);

/* Return size of spooled excess data (data beyond end of PDF data) */
extern
Hq32x2 pdfspool_excess_bytes(
  PDFSPOOL* pdfspool);

/* Read spooled data. */
extern
int32 pdfspool_read(
  PDFSPOOL* pdfspool,
  uint8*    buff,
  int32     len);

/* Finish with a PDF spooler */
extern
void pdfspool_end(
  PDFSPOOL* pdfspool);

#endif /* !__PDFSPOOL_H__ */

/* eof */
