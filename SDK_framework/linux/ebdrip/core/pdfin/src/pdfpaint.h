/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfpaint.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * header file for pdfpaint.c declaring exported functions
 */

#ifndef __PDFPAINT_H__
#define __PDFPAINT_H__

/* ----- Exported functions ----- */
int32 pdfop_f( PDFCONTEXT *pdfc ) ;
int32 pdfop_F( PDFCONTEXT *pdfc ) ;
int32 pdfop_f1s( PDFCONTEXT *pdfc ) ;
int32 pdfop_n( PDFCONTEXT *pdfc ) ;
int32 pdfop_s( PDFCONTEXT *pdfc ) ;
int32 pdfop_S( PDFCONTEXT *pdfc ) ;
int32 pdfop_b( PDFCONTEXT *pdfc ) ;
int32 pdfop_b1s( PDFCONTEXT *pdfc ) ;
int32 pdfop_B( PDFCONTEXT *pdfc ) ;
int32 pdfop_B1s( PDFCONTEXT *pdfc ) ;

#endif /* protection for multiple inclusion */

/* end of file pdfpaint.h */

/* Log stripped */
