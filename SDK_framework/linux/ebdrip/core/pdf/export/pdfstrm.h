/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!export:pdfstrm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF stream API
 */

#ifndef __PDFSTRM_H__
#define __PDFSTRM_H__

#include "swpdf.h"

struct OBJECT ;    /* from COREobjects */


Bool pdf_createfilter( PDFCONTEXT *pdfc, OBJECT *file,
                       struct OBJECT *name, struct OBJECT *args, int32 cst );
Bool pdf_createfilterlist( PDFCONTEXT *pdfc,
                           struct OBJECT *file, struct OBJECT *theo,
                           struct OBJECT *parms, Bool copyparms, int32 cst );
void pdf_flushstreams( PDFCONTEXT *pdfc ) ;

Bool pdf_purgestreams( PDFCONTEXT *pdfc );

/** Estimate the size of the streams that can be purged. */
size_t pdf_measure_purgeable_streams(PDFCONTEXT *pdfc);

Bool pdf_rewindstream(PDFCONTEXT *pdfc, struct OBJECT* stream, Bool* rewound);

/** Restore the position of streams and free their restorefiles entries */
Bool pdf_restorestreams( PDFCONTEXT *pdfc, Bool result );

#endif /* protection for multiple inclusion */

/* Log stripped */
