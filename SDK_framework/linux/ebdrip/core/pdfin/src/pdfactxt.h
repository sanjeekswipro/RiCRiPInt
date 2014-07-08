/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfactxt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Acrobat Form Text Field API
 */

#ifndef __Pdfactxt_Header_Included__
#define __Pdfactxt_Header_Included__
/* PDF AcroForm Text Fields */

#include "charsel.h"  /* for glyph selector */
#include "pdffont.h"

extern int32 pdf_TextFieldAdjust( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pdf_fontdetails );

typedef int32 PDF_GLYPH_CALLBACK_FUNC( void *pState, SYSTEMVALUE CurPos, char_selector_t *pSelector );

#endif  /* __Pdfactxt_Header_Included__ */

/* Log stripped */
