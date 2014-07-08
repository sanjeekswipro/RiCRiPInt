/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!export:pdfhtname.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF name mapping interface
 */

#ifndef __PDFHTNAME_H__
#define __PDFHTNAME_H__

/* ----- Exported functions ----- */
int32 pdf_mapinternalnametopdf( NAMECACHE * internal, OBJECT * external);
int32 pdf_convertpdfnametointernal( OBJECT * nameobj );

#endif /* protection for multiple inclusion */

/* Log stripped */
