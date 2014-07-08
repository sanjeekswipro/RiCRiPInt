/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!export:pdfres.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF resource API
 */

#ifndef __PDFRES_H__
#define __PDFRES_H__

/* ----- External constants ----- */

/* ----- External structures ----- */

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

int32 pdf_add_resource( PDFCONTEXT *pdfc , OBJECT *resource ) ;
void  pdf_remove_resource( PDFCONTEXT *pdfc ) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
