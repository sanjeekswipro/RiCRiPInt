/** \file
 * \ingroup pdfparse
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * $HopeName: SWpdfparse!export:pdfparse.h(EBDSDK_P.1) $
 */

#ifndef __PDFPARSE_H__
#define __PDFPARSE_H__  (1)

#include "std.h"

/* Called by PDF parser to allocate memory */
typedef void* (*SPOOLER_ALLOC)(
  size_t  size);

/* Called by PDF parser to free memory */
typedef void (*SPOOLER_FREE)(
  void*   ptr);

/* Called by PDF parser to add stream data to spooler storage */
typedef HqBool (*SPOOLER_APPEND)(
  void*   spooler_data,
  uint8*  data,
  int32   length);

/* Called by PDF parser to set PDF data length */
typedef void (*SPOOLER_PDFLEN)(
  void*   spooler_data,
  Hq32x2* length);

/* Spooler context used by PDF parser */
typedef struct SPOOLER_IF {
  void*           private_data;
  SPOOLER_APPEND  append;
  SPOOLER_PDFLEN  pdflen;
  SPOOLER_ALLOC   alloc;
  SPOOLER_FREE    free;
} SPOOLER_IF;


/* Return codes */
#define PDFPARSE_PARSE_ERROR      (-3)  /* Parser internal error */
#define PDFPARSE_SPOOL_ERROR      (-2)  /* Error reported by data spooler */
#define PDFPARSE_NOT_PDF          (-1)  /* PDF data did not start %PDF-m.n */
#define PDFPARSE_ERR_UNSPECIFIED  (0)   /* Some unspecified error has occured */
#define PDFPARSE_MORE_DATA        (1)   /* Parser needs more data to find EOJ */
#define PDFPARSE_EOJ              (2)   /* End of PDF job detected */

typedef struct PDF_PARSE PDF_PARSE;

/** /brief Create a new PDF stream parser.
 */
extern
PDF_PARSE* pdfparse_new(
  SPOOLER_IF* spooler);

/** /brief Parse buffered PDF stream.
 */
extern
int32 pdfparse(
  PDF_PARSE*  parse,
  uint8*      buffer,
  int32       length);

/** /brief Complete PDF parsing when there is no more input.
 */
extern
void pdfparse_complete(
  PDF_PARSE*  parse);

/** /brief See if the PDF parser has seen a possible EOF
 */
extern
int32 pdfparse_eof(
  PDF_PARSE*  parse);

/** /brief Destroy a PDF stream parser.
 */
extern
void pdfparse_end(
  PDF_PARSE*  parse);

#endif /* !__PDFPARSE_H__ */


/* eof */
