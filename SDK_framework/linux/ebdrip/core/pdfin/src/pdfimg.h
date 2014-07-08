/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfimg.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Images API
 */

#ifndef __PDFIMG_H__
#define __PDFIMG_H__

/* ----- Exported functions ----- */
int32 pdfop_ID( PDFCONTEXT *pdfc ) ;

int32 pdfimg_dispatch( PDFCONTEXT *pdfc , OBJECT *dict , OBJECT *source ) ;

#endif /* protection for multiple inclusion */

/* end of file pdfimg.h */

/* Log stripped */
