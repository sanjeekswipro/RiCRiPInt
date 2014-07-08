/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfrr.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF retained raster API
 */

#ifndef __PDFRR_H__
#define __PDFRR_H__

#include "md5.h"
#include "swpdf.h"
#include "pdfattrs.h"
#include "pdfexec.h"

/** \defgroup pdfrr PDF Retained Raster.
    \ingroup pdfin */

Bool pdf_rr_init( void ) ;
void pdf_rr_finish( void ) ;

Bool pdf_rr_begin( PDFXCONTEXT *pdfxc ) ;
Bool pdf_rr_walk_pages( PDFCONTEXT *pdfc  , OBJECT *pages ,
                        PDF_SEPARATIONS_CONTROL *pdfsc ) ;
Bool pdf_rr_end( PDFXCONTEXT *pdfxc ) ;

Bool pdf_rr_setup_page( PDFCONTEXT *pdfc ) ;

Bool pdf_rr_start_exec( PDFCONTEXT *pdfc , FILELIST **flptr , int stream ,
                        OBJECT *obj , FILELIST **rr_flptr , void **priv ) ;
void pdf_rr_end_exec( PDFCONTEXT *pdfc , FILELIST **rr_flptr , void **priv ) ;

Bool pdf_rr_start_page( PDFCONTEXT *pdfc, OBJECT *page_dict ,
                        PDF_PAGEDEV *pageDev ) ;
Bool pdf_rr_end_page( PDFCONTEXT *pdfc ) ;

Bool pdf_rr_state_change( PDFCONTEXT *pdfc , Bool skipped , int32 state ) ;

Bool pdf_rr_pre_op( PDFCONTEXT *pdfc , void *op , Bool *skip , void *priv ) ;
Bool pdf_rr_post_op( PDFCONTEXT *pdfc , void *op , void *priv ) ;

Bool pdf_rr_newpagedevice( PDFXCONTEXT *pdfxc ) ;

void init_C_globals_pdfrr( void ) ;
Bool pdf_irrc_init( void ) ;
void pdf_irrc_finish( void ) ;

mm_pool_t pdf_irrc_get_pool( void *connection ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */

