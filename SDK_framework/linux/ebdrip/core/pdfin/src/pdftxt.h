/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdftxt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Text Object Operators API
 */

#ifndef __PDFTXT_H__
#define __PDFTXT_H__


/* pdftxt.h */

#include "pdfexec.h"
#include "pdffont.h"

/* ----- Exported functions ----- */

void pdf_inittextstate( void ) ;

Bool pdf_set_text_matrices( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pdf_fdetails );
Bool pdf_TjJ_showtext( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pfd, OBJECT *pStr );
Bool pdf_cleanup_bt(PDFCONTEXT *pdfc, Bool ok) ;
void  pdf_setTm( PDF_IMC_PARAMS *imc, OMATRIX *matrix );

Bool pdfop_BT( PDFCONTEXT *pdfc ) ;
Bool pdfop_ET( PDFCONTEXT *pdfc ) ;
Bool pdfop_Tc( PDFCONTEXT *pdfc ) ;
Bool pdfop_Tf( PDFCONTEXT *pdfc ) ;
Bool pdfop_TL( PDFCONTEXT *pdfc ) ;
Bool pdfop_Tr( PDFCONTEXT *pdfc ) ;
Bool pdfop_Ts( PDFCONTEXT *pdfc ) ;
Bool pdfop_Tw( PDFCONTEXT *pdfc ) ;
Bool pdfop_Tz( PDFCONTEXT *pdfc ) ;
Bool pdfop_Td( PDFCONTEXT *pdfc ) ;
Bool pdfop_TD( PDFCONTEXT *pdfc ) ;
Bool pdfop_Tm( PDFCONTEXT *pdfc ) ;
Bool pdfop_T1s( PDFCONTEXT *pdfc ) ;
Bool pdfop_Tj( PDFCONTEXT *pdfc ) ;
Bool pdfop_T1q( PDFCONTEXT *pdfc ) ;
Bool pdfop_T2q( PDFCONTEXT *pdfc ) ;
Bool pdfop_TJ( PDFCONTEXT *pdfc ) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
