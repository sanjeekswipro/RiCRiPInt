/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!export:pdfmatch.h(EBDSDK_P.1) $
 * $Id: export:pdfmatch.h,v 1.6.10.1.1.1 2013/12/19 11:25:03 anon Exp $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Specialised form of dictmatch for PDF.
 */

#ifndef __PDFMATCH_H__
#define __PDFMATCH_H__

#include "swpdf.h"    /* PDFCONTEXT */
#include "objecth.h"  /* OBJECT */
#include "dictscan.h" /* NAMETYPEMATCH */

int32 pdf_dictmatch( PDFCONTEXT *pdfc ,
                     OBJECT *dict , NAMETYPEMATCH *match_objects ) ;

#endif /* protection for multiple inclusion */

/*
Log stripped */
