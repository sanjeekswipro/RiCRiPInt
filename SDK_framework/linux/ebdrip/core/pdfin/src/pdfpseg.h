/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfpseg.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Path Segment operators API
 * PURPOSE : header file for pdfpseg.c declaring exported functions
 */

#ifndef __PDFPSEG_H__
#define __PDFPSEG_H__

/* ----- Exported functions ----- */
int32 pdfop_c( PDFCONTEXT *pdfc ) ;
int32 pdfop_l( PDFCONTEXT *pdfc ) ;
int32 pdfop_h( PDFCONTEXT *pdfc ) ;
int32 pdfop_m( PDFCONTEXT *pdfc ) ;
int32 pdfop_re( PDFCONTEXT *pdfc ) ;
int32 pdfop_v( PDFCONTEXT *pdfc ) ;
int32 pdfop_y( PDFCONTEXT *pdfc ) ;

#endif /* protection for multiple inclusion */

/* end of file pdfpseg.h */

/* Log stripped */
