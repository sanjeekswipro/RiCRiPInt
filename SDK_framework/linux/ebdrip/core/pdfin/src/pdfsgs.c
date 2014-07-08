/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfsgs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Special Graphic State Operators
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"

#include "stacks.h"
#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "miscops.h"
#include "gu_ctm.h"
#include "gstack.h"

#include "swpdfin.h"
#include "pdfcntxt.h"
#include "pdfexec.h"
#include "pdfops.h"
#include "pdfin.h"

/* ---------------------------------------------------------------------- */

Bool pdfop_q( PDFCONTEXT *pdfc )
{
  Bool result ;
  GSTATE *oldgs ;
  PDF_IMC_PARAMS *imc ;
  
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  HQASSERT( gstackptr == gstateptr->next, "pdfop_q: gstack is corrupt" ) ;

  /* We swap the existing graphics stack for the current pdf gstack for the
   * so that the pushed gsave goes onto this pdf stack.
   */
  oldgs = gstackptr ;
  gstateptr->next = gstackptr = imc->pdf_graphicstack ;
  gstateptr->theHDLTinfo.next = NULL ; /* For safety */

  result = gs_gpush(GST_GSAVE) ;

  /* Update pdf gstack ptr */
  imc->pdf_graphicstack = gstackptr ;

  /* Restore the original gstack */
  gstateptr->next = gstackptr = oldgs ;
  gstateptr->theHDLTinfo.next = &gstackptr->theHDLTinfo ;

  return result ;
}

Bool pdfop_Q( PDFCONTEXT *pdfc )
{
  Bool result ;
  GSTATE *oldgs ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  HQASSERT( gstackptr == gstateptr->next, "pdfop_Q: gstack is corrupt" ) ;

  if ( ! imc->pdf_graphicstack )
    return error_handler( INVALIDRESTORE ) ;

  /* We switch to the pdf gstack for the grestore */
  oldgs = gstackptr ;
  gstateptr->next = gstackptr = imc->pdf_graphicstack ;

  /* The HDLT info saved by q/Q is ignored, we treat it as if it never
     happened (because the PDF gstates are not part of the normal graphics
     stack when HDLT calls are happening). The HDLTinfo next chain will be
     incorrect during the restore, so we set it to NULL to trap incorrect
     usage. */
  gstackptr->theHDLTinfo = gstateptr->theHDLTinfo ;
  gstackptr->theHDLTinfo.next = NULL ;

  result = gs_setgstate( gstackptr, GST_PDF, FALSE, TRUE, FALSE, NULL ) ;

  /* Update pdf gstack ptr */
  imc->pdf_graphicstack = gstackptr ;

  /* and switch back again afterwards */
  gstateptr->next = gstackptr = oldgs ;
  gstateptr->theHDLTinfo.next = &gstackptr->theHDLTinfo ;

  return result ;
}


Bool pdfop_cq( PDFCONTEXT *pdfc )
{
  UNUSED_PARAM( PDFCONTEXT * , pdfc ) ;
  return gs_cpush() ;
}

Bool pdfop_cQ( PDFCONTEXT *pdfc )
{
  int32 result ;
  GSTATE *oldgs = NULL ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  HQASSERT( gstackptr == gstateptr->next, "pdfop_cQ: gstack is corrupt" ) ;

  /* Need to switch graphicstacks in case the clip stack for the current
   * gstate is empty - but only if the pdf graphic stack isn't empty!
   */
  if ( imc->pdf_graphicstack ) {
    oldgs = gstackptr ;
    gstateptr->next = gstackptr = imc->pdf_graphicstack ;
  }

  result = gs_ctop() ;

  /* Restore the gstacks if we swapped them */
  if ( imc->pdf_graphicstack ) {
    HQASSERT( gstackptr == imc->pdf_graphicstack,
	      "pdfop_cQ: gs_ctop has modified graphics stack stack" ) ;
    gstateptr->next = gstackptr = oldgs ;
  }

  return result ;
}

Bool pdfop_cm( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  int32 i ;
  OMATRIX matrix = {0} ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  
  /* Check stack has at least six operands. */
  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 5 )
    return error_handler( RANGECHECK ) ;
  
  for ( i = 5 ; i >= 0 ; i-- ) {
    /* Next operand */
    if ( !object_get_numeric(stackindex( 5 - i , stack ),
                             &matrix.matrix[ i >> 1 ][ i & 1 ]) )
      return FALSE ;
  }
  
  MATRIX_SET_OPT_BOTH( & matrix ) ;
  
  HQASSERT( matrix_assert( & matrix ) , "result not a proper optimised matrix" ) ;
  
  gs_modifyctm( & matrix ) ;
  
  npop( 6 , stack ) ;
  return TRUE ;
}

/* Get rid of any gstates on the PDF graphics stack. */
void pdf_flush_gstates( PDFCONTEXT *pdfc )
{
  GSTATE *tempptr ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  
  tempptr = imc->pdf_graphicstack ;
  while ( tempptr ) {
    imc->pdf_graphicstack = tempptr->next ;
    gs_discardgstate( tempptr ) ;
    free_gstate( tempptr ) ;
    tempptr = imc->pdf_graphicstack ;
  }
}

Bool pdf_walk_gstack(Bool (*gs_fn)(GSTATE *, void *), void *args)
{
  PDFXCONTEXT *pdfxc ;
  PDFCONTEXT *pdfc ;
  GSTATE *gs ;

  HQASSERT( gs_fn , "gs_fn is null is walk_gframe" ) ;
  
  for ( pdfxc = pdfin_xcontext_base ; pdfxc ; pdfxc = pdfxc->next) {
    for ( pdfc = pdfxc->pdfc ; pdfc->mc != 0 ; pdfc = pdfc->next) {
      HQASSERT( pdfc->u.i , "imc is null in pdf_walk_gframe" ) ;
      for ( gs = pdfc->u.i->pdf_graphicstack ; gs ; gs = gs->next)
	if ( ! ( *gs_fn )( gs , args ))
	  return FALSE ;
      HQASSERT( pdfc->next , "Should be one more pdfc in pdf_walk_gframe" ) ;
    }
    HQASSERT( pdfc->next == NULL , "Next pdfc should be null in pdf_walk_gframe" ) ;
    HQASSERT( pdfc->u.i == NULL , "Last pdfc's imc should be null in pdf_walk_gframe" ) ;
  }
  return TRUE ;
}

/* Log stripped */
