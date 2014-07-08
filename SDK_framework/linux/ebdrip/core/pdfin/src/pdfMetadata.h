/* Copyright (c) 2007 Global Graphics Software Ltd. All Rights Reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: SWpdf!src:pdfMetadata.h(EBDSDK_P.1) $ */
#ifndef _pdfMetadata_h_
#define _pdfMetadata_h_

#include "swpdf.h"
#include "objecth.h"

/** Parse the job-level metadata in the passed stream. Returns FALSE on error.
 */
Bool pdfMetadataParse(PDFCONTEXT* pdfc,
                      FILELIST* metadataStream,
                      OBJECT* metadataDictionary);

#endif

/* Log stripped */

