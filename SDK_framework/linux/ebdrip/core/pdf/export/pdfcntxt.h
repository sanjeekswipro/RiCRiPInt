/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!export:pdfcntxt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF context interface
 */

#ifndef __PDFCNTXT_H__
#define __PDFCNTXT_H__

/* pdfcntxt.h */

#include "objecth.h"
#include "swpdf.h"

/* ----- External constants ----- */

#define PDF_MAX_MC_NESTCOUNT 32    /* Nesting limit of marking contexts */

/* ----- External structures ----- */

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

Bool pdf_find_execution_context( int32 id , PDFXCONTEXT *base ,
                                 PDFXCONTEXT **pdfxc ) ;
Bool pdf_begin_execution_context(PDFXCONTEXT **new_pdfxc, PDFXCONTEXT **base,
                                 PDF_METHODS *methods,
                                 corecontext_t *corecontext) ;
Bool pdf_end_execution_context( PDFXCONTEXT *pdfxc , PDFXCONTEXT **base );

Bool pdf_begin_marking_context( PDFXCONTEXT *pdfxc , PDFCONTEXT **new_pdfc ,
                                OBJECT *resource , int streamtype ) ;
Bool pdf_end_marking_context( PDFCONTEXT *pdfc , OBJECT *resource ) ;

typedef int32 (*PDF_INIT_MC_CALLBACK)( PDF_IMC_PARAMS *imc, void * );

void pdf_set_mc_callback( PDFXCONTEXT *pdfxc, PDF_INIT_MC_CALLBACK init_mc_func,
                          void *arg );

#endif /* protection for multiple inclusion */


/* Log stripped */
