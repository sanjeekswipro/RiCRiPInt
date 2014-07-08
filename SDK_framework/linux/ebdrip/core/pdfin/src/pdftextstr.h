/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdftextstr.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements support functions for PDF text string data structures.
 * (Text streams as well when we need them.)
 */

#ifndef __PDFTEXT_H__
#define __PDFTEXT_H__ (1)

#include "swpdf.h"        /* PDFCONTEXT */
#include "objecth.h"      /* OBJECT */
#include "hqunicode.h"    /* utf8_buffer */

/**
 * \brief Convert a PDF text string to UTF-8 encoding and filtering out any
 * embedded escape sequences.
 *
 * The UTF-16 and -8 encodings are recognised, any other BOM, or lack of BOM
 * will result in the encoding of the source PDF text stream being assumed as
 * PDFDocEncoding.
 *
 * If the PDF Strict flag is set, then only UTF-16BE is recognised, UTF-16LE and
 * UTF-8 BOMs will be ignored and the text string assumed to have
 * PDFDocEncoding.
 *
 * \param[in] pdfc
 * Pointer to a PDF context.
 * \param[in] string
 * PS object containing byte sequence of PDF text string.
 * \param[in] utf8_len
 * Length of buffer in which the UTF-8 version of the PDF text string can be
 * stored in.
 * \param[out] utf8_string
 * Pointer to UTF-8 buffer to hold conversion of PDF text string to UTF-8.
 *
 * \return
 * \c TRUE if the PDF text string is successfully converted to UTF-8, else \c
 * FALSE.
 */
extern
int32 pdf_text_to_utf8(
/*@in@*/ /*@notnull@*/
  PDFCONTEXT*   pdfc,
/*@in@*/ /*@notnull@*/
  OBJECT*       string,
  uint32        utf8_len,
/*@out@*/ /*@notnull@*/
  utf8_buffer*  utf8_string);

#endif /* !__PDFTEXTSTR_H__ */


/* Log stripped */
