/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdffont.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Font handling API
 */

#ifndef __PDFFONT_H__
#define __PDFFONT_H__

/* pdffont.h */

/* Headers */
#include "pdfcmap.h"
#include "pdfcid.h"
#include "pdfencod.h"

/* ----- External constants ----- */

enum {
  FONT_None     = 0, FONT_Type1 = 1, FONT_MMType1 = 2, FONT_Type3 = 3,
  FONT_TrueType = 4, FONT_Type0 = 5, FONT_TypeCID = 6, FONT_UnknownType
} ;

enum { FSUB_None = 0, FSUB_Base14 = 1, FSUB_CID0 = 2, FSUB_CID2 = 3 } ;

enum {
  FSTYLE_Unknown = -1, FSTYLE_None = 0,
  FSTYLE_Bold = 1, FSTYLE_Italic = 2
} ;

#define TYPE3_DICTLEN 8
#define TYPE3_ENCODELEN 256


/* ----- External structures ----- */

/* Introduce new types */
typedef struct pdf_widthdetails PDF_WIDTHDETAILS;
typedef struct pdf_type3details PDF_TYPE3DETAILS;
typedef struct pdf_fontdetails  PDF_FONTDETAILS;

typedef struct pdf_cached_fontdetails PDF_CACHED_FONTDETAILS;


/* Now define them */
struct pdf_widthdetails {
  int32 wid_length ;
  int32 wid_firstchar ;
  int32 wid_lastchar ;
  OBJECT *wid_diffs ;

  int32 wid_validw ;
  int32 wid_mwidth ;
} ;

struct pdf_type3details {
  OBJECT  *charprocs ;
  OBJECT  *fntmatrix ;
  OBJECT  *resources ;
  OBJECT  *encodings ; /* [ 256 ] Once cache in core based on glyph, can remove. */
  OBJECT  *charstrms[ 256 ] ; /* This could be removed if xref lookup didn't rewind. */
} ;

struct pdf_fontdetails {
  /* General fields for diect access */
  OBJECT atmfont ;
  int32  font_type ; /* One of the above enum values */
  int32  font_sub ;
  int32  font_style ; /* Support for styled fonts (fake bold/italics) */
  int32  font_flags ; /* Font descriptor flags */

  /* Not common */
  PDF_WIDTHDETAILS  *width_details ;  /* Type1, TrueType and Type3 specific */
  PDF_TYPE3DETAILS  *type3_details ;  /* Type3 specific */
  PDF_ENCODEDETAILS *encode_details ; /* Not for Type0 or CID */
  PDF_CMAPDETAILS   *cmap_details ;   /* Type0 specific */
  PDF_CIDDETAILS    *cid_details ;    /* CID Type specific */

  /* Type0 */
  OBJECT df_array; /* descendant fonts array ptr */
  int32             num_descendants ;
  PDF_FONTDETAILS **descendants ;
} ;


/* ----- Exported functions ----- */

extern int32 pdf_font_unpack( PDFCONTEXT *pdfc , OBJECT *pdffont,
			      PDF_FONTDETAILS **pdf_fontdetails ) ;
extern void  pdf_flushfontdetails( PDFXCONTEXT *pdfxc, int32 slevel ) ;

extern int32 pdf_findresource(int32 category, OBJECT *name);

extern mps_res_t pdf_scancachedfonts(mps_ss_t ss,
                                     PDF_CACHED_FONTDETAILS *cache);


#endif /* protection for multiple inclusion */


/* Log stripped */
