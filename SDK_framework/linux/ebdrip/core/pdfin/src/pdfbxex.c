/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfbxex.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF BX/EX operator implementation
 */

#include "core.h"
#include "swerrors.h"

#include "pdfbxex.h"
#include "pdfops.h"
#include "pdfmem.h"
#include "pdfdefs.h"
#include "pdfin.h"
#include "pdfx.h"

int32 pdfop_BX( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( ++imc->bxex > BXEX_SEQ_MAX ) {
    HQFAIL( "BX/EX nesting limit exceeded.\n" ) ;
    return error_handler( LIMITCHECK ) ;
  }

  if ( imc->bxex > 0 && imc->bxex <= BXEX_SEQ_MAX )
    imc->bxex_seq[ imc->bxex - 1 ] =
                         theStackSize( imc->pdfstack ) ;
  else {
    HQFAIL( "Badly nested BX/EX ops.\n" ) ;
    return error_handler( UNMATCHEDMARK ) ;
  }

  return TRUE ;
}

int32 pdfop_EX( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( --imc->bxex < 0 ) {
    HQFAIL( "Badly nested BX/EX ops.\n" ) ;
    return error_handler( UNMATCHEDMARK ) ;
  }

  HQASSERT( theStackSize( imc->pdfstack ) ==
      imc->bxex_seq[ imc->bxex ] ,
      "Maybe BX/EX aren't sequence points after all..\n" ) ;

  return TRUE ;
}

int32 pdfop_Unknown( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );

  if ( imc->bxex > 0 ) {
    /* We're inside at least one BX/EX pair.
     * Clear the stack down to the innermost sequence
     * point and swallow the error.
     */
    pdf_clearstack( pdfc ,
                    & imc->pdfstack ,
                    imc->bxex_seq[ imc->bxex - 1 ] ) ;

    /* PDF/X does not appreciate unrecognised operators */
    if (! pdfxUnknownOperator(pdfc))
      return FALSE;
  } else {
    /* Just generate an undefined error. */

    return error_handler( UNDEFINED ) ;
  }

  return TRUE ;
}

/* end of file pdfbxex.c */

/* Log stripped */
