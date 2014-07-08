/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfexec.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF "exec" operators API
 */

#ifndef __PDFEXEC_H__
#define __PDFEXEC_H__


#include "swpdf.h"
#include "objecth.h"
#include "fileioh.h"

/* ----- External constants ----- */

#define PDF_MAX_MC_NESTCOUNT 32    /* Nesting limit of marking contexts */

/* ----- External structures ----- */

/* ----- External global variables ----- */

/* ----- Exported macros ----- */


/* ----- Exported functions ----- */

Bool pdf_execop(ps_context_t *pscontext, int32 opnumber);
Bool pdf_read_trailerdict( PDFCONTEXT *pdfc , FILELIST *flptr ,
                           OBJECT *encrypt , OBJECT *id ) ;
void pdf_setTrapStatus(PDFCONTEXT *pdfc, OBJECT *trapped) ;
Bool pdf_get_content( PDFCONTEXT *pdfc, OBJECT **content ) ;
Bool pdf_next_content( PDFCONTEXT *pdfc , FILELIST **flptr ) ;
Bool pdf_close_tmp_file( PDFCONTEXT *pdfc, FILELIST *flptr ) ;
Bool pdf_extract_trailer_dict(PDFCONTEXT *pdfc ,
                              OBJECT * dict);
Bool pdf_extract_prevtrailer_dict(PDFCONTEXT *pdfc ,
                                  OBJECT * prevobject);
Bool pdf_validate_info_dict(
  PDFCONTEXT *pdfc);

/**
 * This routine commences the processing of the referenced pdf file (while in
 * the context of a prior PDF file) in the manner similar to pdfopen_internal().
 *
 * A separate execution context is created, and the header and trailer are read
 * in as for normal PDF processing.  However, the PDF parameters are inherited
 * from the prior context and the new execution context marked as 'encapsulated'
 * to prevent a separate 'showpage' being generated for the referenced page.
 *
 * Close the new context using pdf_close_internal().
 */
Bool pdf_open_refd_file(PDFCONTEXT **new_pdfc,
                        PDF_IXC_PARAMS *prior_ixc,
                        FILELIST *pFlist,
                        corecontext_t *corecontext);

/**
 * Destroy the execution context associated with the passed PDF context.
 */
Bool pdf_close_internal(PDFCONTEXT *pdfc);

/**
 * Execute the specified page.
 *
 * \param pPageRef The page reference (number or label).
 * \param pPageFound Set to TRUE if the page was located, otherwise FALSE.
 * \return FALSE on error.
 */
Bool pdf_exec_page(PDFCONTEXT *pdfc, OBJECT *pPageRef, Bool *pPageFound);

typedef struct PDF_SEPARATIONS_CONTROL PDF_SEPARATIONS_CONTROL;
Bool pdf_count_pages(PDFCONTEXT *pdfc ,
                     PDF_IXC_PARAMS *ixc,
                     OBJECT *pages ,
                     PDF_SEPARATIONS_CONTROL *pdfsc);
Bool pdf_walk_pages(PDFCONTEXT *pdfc , OBJECT *pages ,
                    PDF_SEPARATIONS_CONTROL *pdfsc);

#endif /* protection for multiple inclusion */


/* Log stripped */
