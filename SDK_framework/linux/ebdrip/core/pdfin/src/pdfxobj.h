/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfxobj.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF extended object (Xobj) handling.
 */

#ifndef __PDFXOBJ_H__
#define __PDFXOBJ_H__

#include "objecth.h"

/* ----- Exported functions ----- */
Bool pdfop_Do( PDFCONTEXT *pdfc ) ;
Bool pdfop_PS( PDFCONTEXT *pdfc ) ;

Bool pdf_XObjectIsForm( PDFCONTEXT *pdfc , Bool *is_form ) ;
Bool pdf_DoExtractingGroup( PDFCONTEXT *pdfc ,
                            Bool requireColorSpace ,
                            SoftMaskType softmasktype ,
                            OBJECT *bgcolor,
                            OBJECT *xferfn,
                            uint32 *extractGroupId) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
