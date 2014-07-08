/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfclip.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Clipping API
 */

#ifndef __PDFCLIP_H__
#define __PDFCLIP_H__


/* pdfclip.h
   PURPOSE : header file for pdfclip.c declaring exported functions
 */


#include "swpdf.h"

/* ----- External structures ----- */

enum CLIP_MODE {
  PDF_NZ_CLIP,
  PDF_EO_CLIP,
  PDF_NO_CLIP
} ;

/* ----- Exported functions ----- */

int32 pdfop_W( PDFCONTEXT *pdfc ) ;
int32 pdfop_W1s( PDFCONTEXT *pdfc ) ;
int32 pdf_check_clip( PDFCONTEXT *pdfc ) ;

#endif /* protection for multiple inclusion */

/* end of file pdfclip.h */

/* Log stripped */
