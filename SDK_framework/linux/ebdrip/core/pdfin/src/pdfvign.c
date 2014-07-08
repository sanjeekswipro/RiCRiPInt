/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfvign.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Vignette operators
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "namedef_.h"   /* for system_names */

#include "stacks.h"
#include "constant.h"
#include "graphics.h"
#include "gstate.h"
#include "gschead.h"
#include "shadex.h"

#include "swpdf.h"
#include "stream.h"
#include "pdfmatch.h"
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdfxref.h"

#include "pdfattrs.h"
#include "pdfcolor.h"
#include "pdfinlop.h"
#include "pdfvign.h"
#include "pdfexec.h"
#include "pdfops.h"
#include "pdfin.h"


Bool pdfop_sh( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  int32 stacksize, result ;
  OBJECT *res ;
  OBJECT *inst ;
  OBJECT *dict ;
  OBJECT psCopy = OBJECT_NOTVM_NOTHING;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  inst = TopStack( *stack , stacksize ) ;

  if ( ! pdf_get_resource( pdfc , NAME_Shading , inst , &res ) )
    return FALSE ;

  /* The resource object should be a stream or a dictionary */

  if ( oType(*res) == OFILE) {

    /* Get the dictionary associated with the stream. */
    if ( NULL == ( dict = streamLookupDict( res )))
      return error_handler( UNDEFINED ) ;
  } else if ( oType(*res) == ODICTIONARY ) {
    dict = res ;
  } else
    return error_handler( TYPECHECK ) ;

  if ( !pdfsh_prepare(pdfc, dict, res) )
    return FALSE ;

  /* Copy the dictionary into postscript memory and stack it. */
  if ( ! (pdf_copyobject(NULL, dict, &psCopy) &&
          push(&psCopy, stack)) )
    return FALSE ;

  /* Call PostScript shfill */
  result = gs_shfill(stack, gstateptr, GS_SHFILL_SHFILL) ;

  npop( 1 , stack ) ;

  return result ;
}

static NAMETYPEMATCH pdfshfill_dictmatch[] = {
/* 0 */ { NAME_ColorSpace,               4, { ONAME, OARRAY, OPACKEDARRAY, OINDIRECT }},
/* 1 */ { NAME_ShadingType,              1, { OINTEGER }},
/* 2 */ { NAME_DataSource   | OOPTIONAL, 5, { OSTRING, OARRAY, OPACKEDARRAY, OFILE, OINDIRECT }},
/* 3 */ { NAME_Function     | OOPTIONAL, 5, { OARRAY, OPACKEDARRAY, ODICTIONARY, OFILE, OINDIRECT }},
        DUMMY_END_MATCH
} ;

/* Prepare a PDF shading stream for use by the PostScript shfill operator */
Bool pdfsh_prepare(PDFCONTEXT *pdfc, OBJECT *dict, OBJECT *stream)
{
  OBJECT *theo ;
  OBJECT mappedObj = OBJECT_NOTVM_NOTHING ;
  OBJECT thecopy = OBJECT_NOTVM_NOTHING ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  HQASSERT(dict, "No dict operand to pdfsh_prepare") ;
  HQASSERT(stream, "No stream operand to pdfsh_prepare") ;
  HQASSERT(oType(*dict) == ODICTIONARY, "dict must be an ODICTIONARY." ) ;
  HQASSERT(oType(*stream) == OFILE ||
           oType(*stream) == ODICTIONARY,
           "stream must be an OFILE or ODICTIONARY" ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* Resolve any indirect references */
  if ( ! pdf_resolvexrefs(pdfc, dict) )
    return FALSE ;

  if ( ! pdf_dictmatch(pdfc, dict, pdfshfill_dictmatch))
    return FALSE ;

  switch ( oInteger(*pdfshfill_dictmatch[1].result) ) {
  case 4: case 5: case 6: case 7: /* Need DataSource */
    if ( oType(*stream) == OFILE ) {
      /* If no DataSource or strict PDF, use stream data */
      if ( ixc->strictpdf || pdfshfill_dictmatch[2].result == NULL ) {
        oName( nnewobj ) = system_names + NAME_DataSource ;
        if ( !pdf_fast_insert_hash(pdfc, dict, &nnewobj, stream) )
          return FALSE ;
      }
    } else if ( ixc->strictpdf ) /* Dictionary not strictly allowed */
      return error_handler(TYPECHECK) ;

    break ;
  case 1: case 2: case 3:
    if ( oType(*stream) != ODICTIONARY )
      return error_handler(TYPECHECK) ;

    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  /* Map ColorSpace to a PostScript one */
  theo = pdfshfill_dictmatch[ 0 ].result ;

  if ( ! pdf_mapcolorspace( pdfc , theo , &mappedObj , NULL_COLORSPACE ))
    return FALSE ;

  /* Take a copy of the mapped ColorSpace to prevent problems when freeing the
   * memory allocated for the shading dictionary. The new object must be unique
   * (i.e. Not stored in the XREF cache) and allocated from the PDF object pool
   * since it is inserted back into the shading dictionary.
   */
  if ( ! pdf_copyobject(pdfc, &mappedObj, &thecopy) )
    return FALSE ;
  pdf_freeobject(pdfc, theo) ;

  /* Stuff copy of mapped ColorSpace back into shading dict */
  oName( nnewobj ) = system_names + NAME_ColorSpace ;
  if ( !pdf_fast_insert_hash(pdfc, dict, &nnewobj, &thecopy) )
    return FALSE ;

  return TRUE ;
}


/* Log stripped */
