/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfsgs.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Special Graphic State Operators API
 */

#ifndef __PDFSGS_H__
#define __PDFSGS_H__

/* ----- External constants ----- */

/* ----- External structures ----- */

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

int32 pdfop_q( PDFCONTEXT *pdfc ) ;
int32 pdfop_Q( PDFCONTEXT *pdfc ) ;
int32 pdfop_cm( PDFCONTEXT *pdfc ) ;
int32 pdfop_cq( PDFCONTEXT *pdfc ) ;
int32 pdfop_cQ( PDFCONTEXT *pdfc ) ;

void pdf_flush_gstates( PDFCONTEXT *pdfc ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
