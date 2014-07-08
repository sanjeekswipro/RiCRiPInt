/** \file
 * \ingroup pdfobj
 *
 * $HopeName: COREpdf_base!export:pdfmem.h(EBDSDK_P.1) $
 * $Id: export:pdfmem.h,v 1.25.10.1.1.1 2013/12/19 11:25:03 anon Exp $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF object and memory allocation/deallocation.
 */

#ifndef __PDFMEM_H__
#define __PDFMEM_H__

/** \defgroup pdfobj PDF Object database
    \ingroup pdf */
/** \{ */

#include "mm.h"
#include "swpdf.h"

/* The simple ones are done as macros. These assume pdfxc has been set
 * up using PDF_GET_XC and so it has already been asserted non-NULL.
 */

#define PDF_ALLOCOBJECT( _pdfxc, _len ) \
    pdf_objmemallocfunc((_len), (_pdfxc))

#define PDF_FREEOBJECT( _pdfxc, _obj , _len ) \
    mm_free( (_pdfxc)->mm_object_pool , \
             ( mm_addr_t )(_obj) , \
             sizeof( OBJECT ) * (_len))

#define PDF_ALLOCSTRING( _pdfxc, _len ) \
    mm_alloc( (_pdfxc)->mm_object_pool , \
              (_len) , \
              MM_ALLOC_CLASS_PDF_PSOBJECT )

#define PDF_FREESTRING( _pdfxc, _obj , _len ) \
    mm_free( (_pdfxc)->mm_object_pool , \
             ( mm_addr_t )(_obj) , \
             (_len))

/* The trickier ones justify function calls. */

/* PDF object allocator takes an execution context for the second parameter,
   but uses void for the prototype to be compatible and usable with dictionary
   extension allocators. */
OBJECT *pdf_objmemallocfunc(int32 numobjs, void * param) ;

void  pdf_clearstack( PDFCONTEXT *pdfc , STACK *pdfstack , int32 seqpoint ) ;
void  pdf_freeobject( PDFCONTEXT *pdfc , OBJECT *pdfobj ) ;
void  pdf_freeobject_from_xc( PDFXCONTEXT *pdfxc , OBJECT *pdfobj ) ;
Bool pdf_copyobject( PDFCONTEXT *pdfc , OBJECT *srcobj , OBJECT *destobj ) ;
Bool pdf_create_dictionary( PDFCONTEXT *pdfc , int32 len , OBJECT *thedict ) ;
void  pdf_destroy_dictionary( PDFCONTEXT *pdfc , int32 len , OBJECT *thedict ) ;
Bool pdf_create_array( PDFCONTEXT *pdfc , int32 len , OBJECT *thearray ) ;
void  pdf_destroy_array( PDFCONTEXT *pdfc , int32 len , OBJECT *thearray ) ;
Bool pdf_create_string( PDFCONTEXT *pdfc , int32 len , OBJECT *thestring ) ;
void  pdf_destroy_string( PDFCONTEXT *pdfc , int32 len , OBJECT *thestring ) ;
Bool pdf_matrix( PDFCONTEXT *pdfc , OBJECT *theo) ;
Bool pdf_buildstring( PDFCONTEXT *pdfc , uint8 *string , OBJECT *theo ) ;

Bool pdf_fast_insert_hash(PDFCONTEXT *pdfc,
                          OBJECT *thed, OBJECT *thekey, OBJECT *theo) ;
Bool pdf_fast_insert_hash_name(PDFCONTEXT *pdfc,
                               OBJECT *thed, int32 nameNumber, OBJECT *theo) ;

/** \} */

#endif /* protection for multiple inclusion */

/*
* Log stripped */
