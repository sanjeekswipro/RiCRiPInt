/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfjtf.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PJTF information retained in the pdfin execution context (ixc) 
 */

#ifndef __PDFJTF_HEADER_INCLUDED
#define __PDFJTF_HEADER_INCLUDED

typedef struct pdf_jt_info {
  OBJECT *pPageArray;    /* Pointer to the array of PageRange objects */
  OBJECT *pTrapping;     /* Single hierarchy of dictionaries */
  OBJECT *pTParamsJTC;   /* TrapParameter sets defined in JobTicketContents */
  OBJECT *pTParamsDOC;   /* TrapParameter sets defined in the Document object */
  OBJECT *pTRegionsJTC;  /* TrapRegions defined in JobTicketContents */
  OBJECT *pTRegionsDOC;  /* TrapRegions defined in the Document object */
} PJTFinfo;

/* Entry points to the PJTF module. */
extern int32 pdf_jt_contents( PDFCONTEXT *pdfc, OBJECT *pJT );
extern int32 pdf_jt_get_trapinfo( PDFCONTEXT *pdfc, OBJECT *pMediaBox );


#endif /* __PDFJTF_HEADER_INCLUDED */

/* Log stripped */
