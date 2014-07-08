/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfinlop.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF inline operators API
 */

#ifndef __PDFINLOP_H__
#define __PDFINLOP_H__


/* pdfinlop.h */

/* ----- Exported macros ----- */

/* ----- Exported types ----- */
typedef int32 (* PDFINLOP_DISPATCHFN)( PDFCONTEXT *pdfc , OBJECT *dict , OBJECT *source ) ;

/* ----- Exported functions ----- */
extern int32 pdfop_IVD( PDFCONTEXT *pdfc , PDFINLOP_DISPATCHFN thefn ) ;
extern int32 pdfop_BIV( PDFCONTEXT *pdfc ) ;
extern int32 pdfop_EIV( PDFCONTEXT *pdfc ) ;

#endif /* protection for multiple inclusion */

/* end of file pdfinlop.h */

/* Log stripped */
