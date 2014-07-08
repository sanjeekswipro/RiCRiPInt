/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfrefs.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF references API
 */

#ifndef __PDFREFS_H__
#define __PDFREFS_H__


/* Functions exported by pdfrefs.c */
Bool pdf_Ref_dispatch( PDFCONTEXT *pdfc,
                       OBJECT *pRefDict,
                       OBJECT *pBBox,
                       OBJECT *pMatrix,
                       OBJECT *pMetadata,
                       Bool   *pWasRendered );


#endif /* protection for multiple inclusion */


/* Log stripped */
