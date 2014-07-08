/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfclip.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Clipping operators
 */

#include "core.h"
#include "pdfclip.h"

#include "gstack.h" /* gstateptr */
#include "graphics.h" /* theIPathInfo */
#include "graphict.h" /* EOFILL_TYPE */
#include "clipops.h" /* gs_addclip */
#include "pathcons.h" /* gs_newpath */

#include "pdfin.h"


/* ---------------------------------------------------------------------- */

int32 pdfop_W( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  /* We don't actually add a clip path until a path paint
   * operator has been called. */
  imc->pdfclipmode = PDF_NZ_CLIP ;
  return TRUE ;
}

int32 pdfop_W1s( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  /* We don't actually add a clip path until a path paint
   * operator has been called. */
  imc->pdfclipmode = PDF_EO_CLIP ;
  return TRUE ;
}

int32 pdf_check_clip( PDFCONTEXT *pdfc )
{
  int32 clipmode ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  clipmode = imc->pdfclipmode ;
  if ( clipmode != PDF_NO_CLIP )
  {
    /*
     * Ignore empty path clip. c.f. Acrobat
     * If we have
     *   "no-path W n path1 fill path2 fill"
     * or
     *   "no-path W path1 fill path2 fill"
     * Then in both cases Acrobat fills path1, then makes path1 the new clip
     *
     * Also have issues with
     *   "h W n path1 fill..."
     * where a path of just "h" leaves the path empty but does NOT trigger
     * the 'ignore empty path clip' behaviour. So we need the extra
     * seenclosepath flag to modify the behaviour.
     */
    if ( gstateptr->thepath.firstpath == NULL && !imc->seenclosepath )
      return TRUE;
    imc->pdfclipmode = PDF_NO_CLIP ;

    /* Clip mode must be either NZFILL_TYPE or EOFILL_TYPE */

    if ( clipmode == PDF_NZ_CLIP )
      return gs_addclip( NZFILL_TYPE, & ( theIPathInfo( gstateptr )) , FALSE ) ;
    else { /* clipmode == PDF_EO_CLIP */
      HQASSERT( clipmode == PDF_EO_CLIP, "clip mode not valid in pdfclip" ) ;
      return gs_addclip( EOFILL_TYPE, & ( theIPathInfo( gstateptr )) , FALSE ) ;
    }
  }
  imc->seenclosepath = FALSE;

  return gs_newpath() ;
}

/* end of file pdfclip.c */

/* Log stripped */
