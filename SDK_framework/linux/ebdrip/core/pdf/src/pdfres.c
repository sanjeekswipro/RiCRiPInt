/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!src:pdfres.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF resource implementation
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "objects.h"
#include "mm.h"

#include "swpdf.h"
#include "pdfxref.h"
#include "pdfres.h"

Bool pdf_add_resource( PDFCONTEXT *pdfc , OBJECT *resource )
{
  PDF_DICTLIST *newobj ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  HQASSERT( resource , "resource NULL in pdf_add_resource" ) ;

  newobj = (PDF_DICTLIST *) mm_alloc( pdfxc->mm_structure_pool ,
                                      sizeof( PDF_DICTLIST ) ,
                                      MM_ALLOC_CLASS_PDF_RESOURCE ) ;
  if ( NULL == newobj )
    return error_handler( VMERROR ) ;

  /* Resolve indirect reference */
  if (oType(*resource) == OINDIRECT) {
    OBJECT *xref ;

    if ( ! pdf_lookupxref( pdfc , &  xref ,
                           oXRefID(*resource) ,
                           theGen(*resource) ,
                           FALSE ))
      return FALSE ;

    newobj->dict = xref ;
  }
  else
    newobj->dict = resource ;

  newobj->imc = pdfc ;

  HQASSERT( oType(*newobj->dict) == ODICTIONARY,
            "pdf_add_resource: resource is not a dictionary" ) ;

  newobj->next = pdfc->pdfenv ;
  pdfc->pdfenv = newobj ;

  /* Invalidate the resource cache. */
  {
    int32 i ;
    int32 n = sizeof( pdfc->resource_cache ) / sizeof( PDF_RESOURCE_CACHE ) ;
    for ( i = 0 ; i < n ; ++ i ) {
      pdfc->resource_cache[ i ].resource = NULL ;
      pdfc->resource_cache[ i ].valid = FALSE ;
    }
  }

  return TRUE ;
}

void pdf_remove_resource( PDFCONTEXT *pdfc )
{
  PDF_DICTLIST *dptr ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  HQASSERT( pdfc->pdfenv , "pdfenv NULL in pdf_remove_resource" ) ;

  dptr = pdfc->pdfenv ;
  pdfc->pdfenv = pdfc->pdfenv->next ;

  mm_free( pdfxc->mm_structure_pool , dptr , sizeof( PDF_DICTLIST ) ) ;

  /* Invalidate the resource cache. */
  {
    int32 i ;
    int32 n = sizeof( pdfc->resource_cache ) / sizeof( PDF_RESOURCE_CACHE ) ;
    for ( i = 0 ; i < n ; ++ i ) {
      pdfc->resource_cache[ i ].resource = NULL ;
      pdfc->resource_cache[ i ].valid = FALSE ;
    }
  }
}

/* end of file pdfres.c */

/* Log stripped */
