/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfmc.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Marked Content API
 */

#ifndef __PDFMC_H__
#define __PDFMC_H__

/* pdfmc.h */

#include "pdfexec.h"

typedef struct pdf_mc_stack_t pdf_mc_stack;

extern Bool pdfop_BMC( PDFCONTEXT *pdfc ) ;
extern Bool pdfop_BDC( PDFCONTEXT *pdfc ) ;
extern Bool pdfop_EMC( PDFCONTEXT *pdfc ) ;
extern Bool pdfop_MP( PDFCONTEXT *pdfc ) ;
extern Bool pdfop_DP( PDFCONTEXT *pdfc ) ;
extern void pdf_mc_freeall(PDF_IMC_PARAMS *imc, mm_pool_t pool);

#endif /*__PDFMC_H__*/
/* end of file pdfmc.h */

/* Log stripped */
