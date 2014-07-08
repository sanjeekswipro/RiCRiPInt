/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdftype3.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF User-defined (Type 3) Font routines
 */

#include "core.h"
#include "swerrors.h"

#include "objects.h"
#include "stacks.h"
#include "graphics.h"
#include "gschead.h"
#include "gstack.h"
#include "fcache.h"
#include "fonth.h"

#include "pdfexec.h"
#include "pdfops.h"
#include "pdfin.h"


/* ---------------------------------------------------------------------- */
Bool pdfop_d0( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;
  charcontext_t *charcontext ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  charcontext = char_current_context() ;
  if (charcontext == NULL) {
    /* no context, so ignore */
    npop( 2 , stack ) ;
    return TRUE;
  } else {
    return gs_setcharwidth( stack ) ;
  }
}

/* -------------------------------------------------------------------------- */
Bool pdfop_d1( PDFCONTEXT *pdfc )
{
  OBJECT colorspace = OBJECT_NOTVM_NOTHING ;
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;

  /* firstly copy the fill color over the stroke color in order to
     ensure we use the fill color always in the glyph content stream
     (we are protected inside a gsave/grestore already */

  if (!(gsc_currentcolorspace(gstateptr->colorInfo, GSC_FILL, &colorspace) &&
         push(&colorspace, &operandstack)))
    return FALSE;

  if (!gsc_setcolorspace(gstateptr->colorInfo, &operandstack, GSC_STROKE))
    return FALSE;

  if (!gsc_currentcolor(gstateptr->colorInfo, &operandstack, GSC_FILL))
    return FALSE;

  if (!gsc_setcolor(gstateptr->colorInfo, &operandstack, GSC_STROKE))
    return FALSE;


  /* See red book 2 page 383; note: the array returned must be that given,
   * unless it wasn't an array, in which case the one we put in
   * internaldict is used.
   */
  return gs_setcachedevice( stack , FALSE ) ;
}



/* Log stripped */
