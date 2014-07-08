/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfstrobj.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Handling of PDF stream objects API.
 */

#ifndef __PDFSTROBJ_H__
#define __PDFSTROBJ_H__

#include "pdfxref.h"

/**
 * Find an object within a compressed object stream.
 *
 * \param pflptr On successful exit from this function, this will be set to a
 * FILELIST positioned immediately before the requested object, allowing it to
 * be read by the caller.
 * \param objectStream Reference to the compressed object stream containing
 * (or extending an object stream containing) the target object.
 * \param targetObjNum The number of the target object within the compressed
 * object stream.
 * \return FALSE on error.
 */
Bool pdf_seek_to_compressedxrefobj(PDFCONTEXT *pdfc,
                                   FILELIST **pflptr,
                                   XREFOBJ *objectStream,
                                   int32 targetObjNum);

int32 pdf_streamobject( PDFCONTEXT *pdfc , FILELIST *flptr , OBJECT *pdfobj ,
                        int32 objnum , int32 objgen, PDF_STREAM_INFO * info ) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
