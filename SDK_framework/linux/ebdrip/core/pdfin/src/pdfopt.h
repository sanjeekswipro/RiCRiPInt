/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfopt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Optional Content API
 */

#ifndef __PDFOPT_H__
#define __PDFOPT_H__

#include "swpdf.h"

typedef struct pdf_ocproperties_s pdf_ocproperties;

extern Bool pdf_oc_getstate_fromprops( PDFCONTEXT *pdfc, OBJECT * properties, Bool * state);
extern Bool pdf_oc_getstate_OCG_or_OCMD(PDFCONTEXT *pdfc, OBJECT * dict, Bool * state);
extern void pdf_oc_freedata( mm_pool_t pool, pdf_ocproperties * props );
extern Bool pdf_oc_getproperties( PDFCONTEXT *pdfc,mm_pool_t pool, pdf_ocproperties ** oc_props, OBJECT *dict );

#endif /* __PDFOPT_H__ */

/* Log stripped */
