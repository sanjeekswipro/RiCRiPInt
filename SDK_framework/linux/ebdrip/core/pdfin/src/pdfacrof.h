/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfacrof.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF API for Acrobat Forms
 */


#ifndef __pdfacrof_Header_Included__
#define __pdfacrof_Header_Included__



/* Following value used because zero is meaningful for
** the attributes 'FieldFlags' 'Quadding' and 'MaxLen'
** in the PDF_FORMFIELD structure.
*/
#define ACROFORM_ATTR_UNDEFINED    -1

/* 'Quadding' attribute should take only the following values */
#define ACROFORM_QUAD_LEFT   0  /* Default value */
#define ACROFORM_QUAD_CENTRE 1
#define ACROFORM_QUAD_RIGHT  2

/* The following values should be treated as bit-masks for the 'Field Type' 
** attribute 
*/
#define ACROFORM_FLAG_MULTILINE  0x1000   /* Bit 13 = multiline */
#define ACROFORM_FLAG_PASSWORD   0X2000   /* Bit 14 = password  */


/*
** The following structure lists all the values we're interested in 
** from field dictionaries and which are inheritable through the 
** hierarchy of field dictionaries.
*/
typedef struct 
{
  OBJECT *pFieldType;   /* Required: Name: Btn = button; Tx = text; Ch = Choice; Sig = signature */
  int32  FieldFlags;    /* Optional: Flags; bit settings */
  OBJECT *pValue;       /* Optional: Value - string */
  OBJECT *pDefValue;    /* Optional: Default value - string */
  OBJECT *pDefRsrcs;    /* Required iff variable text: Default resource dictionary */
  OBJECT *pDefAppear;   /* Required iff variable text: Default appearances string */
  int32   Quadding;     /* Required iff variable text: Quadding: 0 = left; 1 = centre; 2 = right; so -1 = undefined. */
  int32   MaxLen;       /* Optional: Max num chars.  -1 = undefined */
} PDF_FORMFIELD;



/* ---  Exported function prototypes  --- */

extern int32 pdf_get_AcroForm( PDFCONTEXT *pdfc, PDF_IXC_PARAMS *ixc, OBJECT *pDictObj );
extern int32 pdf_AscendFields( PDFCONTEXT *pdfc, PDF_FORMFIELD *pValues, OBJECT *pDictObj );
extern int32 pdf_NormaliseRect( OBJECT *pRect );

#endif

/* Log stripped */
