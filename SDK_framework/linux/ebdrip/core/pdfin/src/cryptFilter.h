/* Copyright (c) 2007 Global Graphics Software Ltd. All Rights Reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: SWpdf!src:cryptFilter.h(EBDSDK_P.1) $ */

#ifndef _cryptFilter_h_
#define _cryptFilter_h_

#include "fileio.h"

/** Initialise the passed filter as a Crypt filter. Crypt filters are a PDF 1.5
 * feature which allow the document-level encryption filter to be overridden for
 * a stream. The main use of this feature is to disable encryption by means of
 * an 'identity' filter.
 */
void crypt_decode_filter(FILELIST *flptr);

#endif

/* Log stripped */

