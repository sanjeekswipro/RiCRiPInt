/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfcolor.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Colorspace API
 */

#ifndef __PDFCOLOR_H__
#define __PDFCOLOR_H__


/* pdfcolor.h
   PURPOSE : header file for pdfcolor.c declaring exported functions
 */

#include "pdfexec.h"

/* Since pdf_mapcolorspace is called, recursively, on the base
 * colorspace, passing the colorspace in for validation, the main call
 * needs a null colorspace argument (A number not equal to
 * NAME_Pattern, NAME_Indexed or NAME_Separation). */
#define NULL_COLORSPACE 0

/* ----- Exported functions ----- */
int32 pdf_getColorSpace(OBJECT *obj);
int32 pdf_mapcolorspace( PDFCONTEXT *pdfc , OBJECT *srcobj , OBJECT *destobj ,
                         int32 parentcspace ) ;

int32 pdfop_g( PDFCONTEXT *pdfc ) ;
int32 pdfop_G( PDFCONTEXT *pdfc ) ;
int32 pdfop_k( PDFCONTEXT *pdfc ) ;
int32 pdfop_K( PDFCONTEXT *pdfc ) ;
int32 pdfop_rg( PDFCONTEXT *pdfc ) ;
int32 pdfop_RG( PDFCONTEXT *pdfc ) ;
int32 pdfop_sc( PDFCONTEXT *pdfc ) ;
int32 pdfop_SC( PDFCONTEXT *pdfc ) ;
int32 pdfop_scn( PDFCONTEXT *pdfc ) ;
int32 pdfop_SCN( PDFCONTEXT *pdfc ) ;
int32 pdfop_cs( PDFCONTEXT *pdfc ) ;
int32 pdfop_CS( PDFCONTEXT *pdfc ) ;
int32 pdfop_ri( PDFCONTEXT *pdfc ) ;

int32 pdf_set_rendering_intent( PDFCONTEXT *pdfc, OBJECT *theo ) ;

Bool pdf_mapBlendSpace(PDFCONTEXT* pdfc,
                       OBJECT colorspace,
                       OBJECT* mappedcolorspace);

#endif /* protection for multiple inclusion */

/* end of file pdfcolor.h */

/* Log stripped */
