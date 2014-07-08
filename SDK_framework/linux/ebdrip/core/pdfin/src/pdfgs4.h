/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfgs4.h(EBDSDK_P.1) $
 * $Id: src:pdfgs4.h,v 1.11.10.1.1.1 2013/12/19 11:25:14 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Gstate interface for transparency state (PDF 1.4).
 */

#ifndef __PDFGS4_H__
#define __PDFGS4_H__

#include "swpdf.h"
#include "objecth.h"

/* --Public methods-- */

Bool pdf_setBlendMode(PDFCONTEXT* pdfc, OBJECT object);
Bool pdf_setSoftMask(PDFCONTEXT* pdfc, OBJECT object);
Bool pdf_setGrayImageAsSoftMask(PDFCONTEXT* pdfc,
                                OBJECT image);

#endif

/* Log stripped */
