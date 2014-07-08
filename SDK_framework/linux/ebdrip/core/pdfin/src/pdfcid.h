/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfcid.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF CID Font API
 */

#ifndef __PDFCID_H__
#define __PDFCID_H__


/* ----- External constants ----- */

/* ----- External structures ----- */

typedef struct pdf_ciddetails   PDF_CIDDETAILS;

struct pdf_ciddetails { /* placeholder */
  OBJECT cidsysinfo; /* Probably unused */

  /* Horizontal write mode stuff */
  OBJECT       w;
  SYSTEMVALUE dw;

  /* Vertical write mode stuff */
  OBJECT       w2;
  SYSTEMVALUE dw2[3];

  /* TrueType CID mapping */
  int32 ncidtogids ;
  uint16 *cidtogid ;
} ;

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

extern int32 pdf_setciddetails(PDFCONTEXT *pdfc,
			       PDF_CIDDETAILS *cid_details,
			       OBJECT *fontdict, int32 cidtype);

extern int32 pdf_getcid_w(PDF_CIDDETAILS *cid_details, int32 cid,
			  SYSTEMVALUE *width);

extern int32 pdf_getcid_w2(PDF_CIDDETAILS *cid_details, int32 cid,
			   SYSTEMVALUE *widths);

int32 pdf_getcid_gid(PDF_CIDDETAILS *cid_details, int32 cid, int32 *gid);

#endif /* protection for multiple inclusion */




/* Log stripped */
