/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfattrs.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Attributes Access API
 */

#ifndef __PDFATTRS_H__
#define __PDFATTRS_H__


/* pdfattrs.h */

#include "pdfexec.h"

/* ----- External constants ----- */

/* ----- External structures ----- */

typedef struct pdf_pagedev {
  OBJECT *MediaBox ;
  OBJECT *CropBox ;
  OBJECT *ArtBox ;
  OBJECT *TrimBox ;
  OBJECT *BleedBox ;
  OBJECT *Rotate ;
  OBJECT *UserUnit ; /* PDF1.6: the default (number) is 1.0 = 1/72 inch */
  NAMECACHE *PlateColor ;
} PDF_PAGEDEV ;

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

Bool pdf_get_resource( PDFCONTEXT *pdfc , uint16 name ,
                       OBJECT *inst , OBJECT **resobj ) ;

Bool pdf_get_resourceid( PDFCONTEXT *pdfc , uint16 name ,
                         OBJECT *rsrc_inst , OBJECT *resobj ) ;

int32 pdf_setpagedevice( PDFXCONTEXT *pdfxc ,
                         PDF_PAGEDEV *pagedev ,
                         int32 *mbox_gt_inch ,
                         SYSTEMVALUE *translation ) ;

void  pdf_getpagedevice( PDFXCONTEXT *pdfxc ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
