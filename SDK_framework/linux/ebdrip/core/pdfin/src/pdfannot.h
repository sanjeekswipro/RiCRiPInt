/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfannot.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Annotations API
 */

#ifndef __pdfannot_Header_Included__
#define __pdfannot_Header_Included__

/* The following are bit-masks defining flag values for
** the 'F' key in the annotation dictionary.
*/
#define ANNOT_FLAG_HIDDEN   2
#define ANNOT_FLAG_PRINT    4


/* Exported functions */
extern int32 pdf_SubmitField( PDFCONTEXT *pdfc, OBJECT *pApDict,
                              OBJECT *pApStrm, OBJECT *pDefRsrcs );

extern int32 pdf_ResolveAppStrm( PDFCONTEXT *pdfc, OBJECT **ppApDict, OBJECT *pApState );
extern int32 pdf_do_annots( PDFCONTEXT *pdfc, OBJECT *list, int32 len );


#endif

/* Log stripped */
