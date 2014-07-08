/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfshow.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Text "show" operators API
 */

#ifndef __PDFSHOW_H__
#define __PDFSHOW_H__

/* pdfshow.h */

#include "pdfactxt.h"


/* ----- External constants ----- */

/* ----- External structures ----- */

typedef struct {  /* Parameters for 'pdf_stringwidth()' */
  SYSTEMVALUE  Tx;
  SYSTEMVALUE  Ty;
  PDF_GLYPH_CALLBACK_FUNC  *CallBackFn;
  void *pState;
} PDF_SW_PARAMS;



/* ----- External global variables ----- */

extern OMATRIX italic_matrix ;

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

extern int32 pdf_setTCJM( PDFCONTEXT *pdfc,
			  OMATRIX *pq ) ;

extern int32 pdf_setTRM( PDFCONTEXT *pdfc,
			 OMATRIX *pq,
			 OBJECT *fntmatrix ) ;

extern int32 pdf_show( PDFCONTEXT *pdfc,
		       PDF_FONTDETAILS *pdf_fontdetails,
                       OBJECT *theo, int32 type, PDF_SW_PARAMS *pSWParams ) ;

extern int32 pdf_stringwidth( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pdf_fontdetails,
                       OBJECT *pString, SYSTEMVALUE *pTx, SYSTEMVALUE *pTy,
                       PDF_GLYPH_CALLBACK_FUNC *pCb, void *pState );

#endif /* protection for multiple inclusion */


/* Log stripped */
