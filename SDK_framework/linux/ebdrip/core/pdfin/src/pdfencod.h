/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfencod.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Font Encoding API
 */

#ifndef __PDFENCOD_H__
#define __PDFENCOD_H__

/* pdfencod.h */

/* ----- External constants ----- */

enum { ENC_None=0, ENC_Embedded=1, ENC_MacRoman=2, ENC_MacExpert=3, ENC_WinAnsi=4 } ;

/* ----- External structures ----- */

typedef struct pdf_encodedetails {
  int32 enc_type ;	/* Encoding Type. */
  
  int32 enc_subtype ;	/* Sub entries for Encoding dictionary. */
  int32 enc_length ;
  int16 enc_offset[ 256 ] ;
  OBJECT *enc_diffs ;
} PDF_ENCODEDETAILS ;

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

extern int32 pdf_setencodedetails( PDFCONTEXT *pdfc ,
				   PDF_ENCODEDETAILS *pdf_encodedetails ,
				   OBJECT *encoding ) ;
extern int32 pdf_getencoding( PDF_ENCODEDETAILS *pdf_encodedetails ,
                              int32 *charcode , NAMECACHE **charname ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
