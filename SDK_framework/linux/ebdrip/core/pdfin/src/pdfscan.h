/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfscan.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Lexical analysis API
 */

#ifndef __PDFSCAN_H__
#define __PDFSCAN_H__

#include "swpdf.h" /* PDF_STREAM_INFO */

/* General scan functions which read directly from job stream. */
int32 pdf_xrefobject( PDFCONTEXT *pdfc, FILELIST *flptr, OBJECT *pdfobj, PDF_STREAM_INFO *info, Bool streamDictOnly, int8 *streamDict ) ;
int32 pdf_xrefstreamobj ( PDFCONTEXT *pdfc, FILELIST *flptr, OBJECT *pdfobj ) ;
int32 pdf_readobject ( PDFCONTEXT *pdfc, FILELIST *flptr, OBJECT *pdfobj ) ;

int32 pdf_scancontent( PDFCONTEXT *pdfc, FILELIST **flptr, OBJECT *pdfobj ) ;

int32 pdf_readdata( FILELIST *flptr, uint8 *lineptr, uint8 *lineend ) ;
int32 pdf_readdata_delimited( FILELIST *flptr, uint8 *lineptr, uint8 *lineend ) ;

Bool pdf_scan_next_integer( FILELIST *flptr, int32 *intvalue,
                            SYSTEMVALUE *fltvalue, int32 *intflt );
Bool pdf_scan_next_integer_with_bytescount( FILELIST *flptr, int32 *intvalue,
                                            SYSTEMVALUE *fltvalue, int32 *intflt,
                                            int32 *bytescount );

/* Special xref scan functions which read directly from job stream. */
Bool pdf_scan_xref_required_whitespace( FILELIST *flptr ) ;
Bool pdf_scan_xref_required_integer( FILELIST *flptr, int32 *integer ) ;
Bool pdf_scan_xref_optional_string( FILELIST *flptr, uint8 *token, int32 token_len ) ;

/* Scan functions which read from an existing buffer. */
Bool pdf_scan_integer( uint8 *lineptr, uint8 *lineend, int32 *integer ) ;
Bool pdf_scan_large_integer( uint8 *lineptr, uint8 *lineend, Hq32x2 *integer );
int32 pdf_scan_integer_allowing_spaces( uint8 *lineptr, uint8 *lineend, int32 *integer ) ;

#endif

/* Log stripped */
