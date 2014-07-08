/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfgstat.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Graphics State operators API
 * PURPOSE : header file for pdfgstat.c declaring exported functions
 */

#ifndef __PDFGSTAT_H__
#define __PDFGSTAT_H__


/* ----- Exported functions ----- */
int32 pdfop_d( PDFCONTEXT *pdfc );
int32 pdfop_gs( PDFCONTEXT *pdfc );
int32 pdfop_i( PDFCONTEXT *pdfc );
int32 pdfop_j( PDFCONTEXT *pdfc );
int32 pdfop_J( PDFCONTEXT *pdfc );
int32 pdfop_M( PDFCONTEXT *pdfc );
int32 pdfop_w( PDFCONTEXT *pdfc );

int32 pdf_set_extgstate( PDFCONTEXT *pdfc, OBJECT *res );
int32 pdf_map_function( PDFCONTEXT *pdfc , OBJECT *theo ) ;

#endif /* protection for multiple inclusion */

/* end of file pdfgstat.h */

/* Log stripped */
