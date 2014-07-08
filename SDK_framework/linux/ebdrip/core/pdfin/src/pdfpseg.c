/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfpseg.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Path Segment operators
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"

#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "gstate.h"
#include "fontops.h"
#include "stacks.h"
#include "gu_cons.h"
#include "gu_path.h"
#include "pathcons.h"

#include "pdfexec.h"
#include "pdfpseg.h"
#include "pdfops.h"
#include "pdfin.h"

/* ---------------------------------------------------------------------- */

int32 pdfop_c( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  SYSTEMVALUE args[ 6 ] ;
  PDF_IMC_PARAMS *imc ;
  
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  
  stack = ( & imc->pdfstack ) ;
  
  /* Read the first 6 items off the stack, into args */
  if ( !stack_get_numeric(stack, args, 6) )
    return FALSE ;
  
  /* Add 'curve to' to graphics state */
  /* TRUE paramter indicates curve ctrl pts are absolute. */
  if ( ! gs_curveto( TRUE, TRUE, args, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
  
  npop( 6 , stack ) ;
  return TRUE ;
}

int32 pdfop_l( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  SYSTEMVALUE args[ 2 ] ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  
  stack = & ( imc->pdfstack ) ;
  
  if ( !stack_get_numeric(stack, args, 2) )
    return FALSE ;
  
  /* Add 'line to' to graphics state */
  /* TRUE parameter indicates line absolute. */
  if ( ! gs_lineto( TRUE, TRUE, args, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
  
  npop( 2 , stack ) ;
  return TRUE ;
}

/**
 * PDF 'h' closepath operator
 *
 * 'h' and the PostScript closepath operator behave very slightly differently.
 * This is not clear from the PDF spec, and is possibly an Acrobat bug.
 * In the PS spec it says closepath is a no-op if either the path is already
 * closed OR there is no currenpoint. The PDF spec drops the second condition.
 * Which sort of implies that a closepath without a current point means
 * something in PDF, but what the semantics could be is anybodys guess.
 * The only differentiating behaviour discoverd with Acrobat is that 'W n' and
 * 'h W n' are not treated the same. i.e. in PostScript terms, 'newpath clip'
 * and 'newpath closepath clip' produce different results.
 *
 * Reverse engineering seems to imply that this is actually an Acrobat bug
 * caused by the path implementation having a boolean saying
 * "is this path empty" which is separate from and independent of whether 
 * there are actually any points in the path. So the Acrobat implementation
 * of 'h' sets "is this path empty" unconditionally to false, but only adds
 * a closepath if there is a currentpoint. This can result in a path with
 * no points having a "is this path empty" state of false. This seems to
 * explain Acrobat behaviour. So ScriptWorks mimics this odd behaviour by
 * having the 'seenclosepath' flag which tracks whether we have had a closepath
 * even on an empty path.
 */
int32 pdfop_h( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc;

  PDF_CHECK_MC(pdfc);
  PDF_GET_IMC( imc ) ;

  imc->seenclosepath = TRUE;
  return path_close( CLOSEPATH, & ( theIPathInfo( gstateptr ))) ;
}

int32 pdfop_m( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  SYSTEMVALUE args[ 2 ] ;
  PDF_IMC_PARAMS *imc ;
  
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  
  stack = & ( imc->pdfstack ) ;

  if ( !stack_get_numeric(stack, args, 2) )
    return FALSE ;
  
  /* Add 'move to' to graphics state */
  /* TRUE parameter indicates move absolute. */
  if ( ! gs_moveto( TRUE, args, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
  
  npop( 2 , stack ) ;
  return TRUE ;
}

int32 pdfop_re( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  SYSTEMVALUE args[ 4 ] ;
  SYSTEMVALUE twoargs[ 2 ] ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  
  stack = ( & imc->pdfstack ) ;
  
  if ( !stack_get_numeric(stack, args, 4) )
    return FALSE ;
  
  /* Implement rectangle by basically doing... */
  /* x y m */
  /* x+width y l */
  /* x+width y+height l */
  /* x y+height l */
  /* h */
  
  /* Add 'move to' to graphics state */
  /* TRUE parameter indicates move absolute. */
  twoargs[ 0 ] = args[ 0 ] ; /* x */
  twoargs[ 1 ] = args[ 1 ] ; /* y */
  if ( ! gs_moveto( TRUE, twoargs, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
  
  /* Add 'line to' to graphics state */
  /* TRUE parameter indicates line absolute. */
  twoargs[ 0 ] += args[ 2 ] ; /* x + width */
  if ( ! gs_lineto( TRUE, TRUE, twoargs, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
  
  /* Add 'line to' to graphics state */
  /* TRUE parameter indicates line absolute. */
  twoargs[ 1 ] += args[ 3 ] ; /* y + height */
  if ( ! gs_lineto( TRUE, TRUE, twoargs, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
  
  /* Add 'line to' to graphics state */
  /* TRUE parameter indicates line absolute. */
  twoargs[ 0 ] = args[ 0 ] ; /* x */
  if ( ! gs_lineto( TRUE, TRUE, twoargs, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
  
  if ( ! path_close( CLOSEPATH, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
    
  npop( 4 , stack ) ;
  return TRUE ;
}

int32 pdfop_v( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  SYSTEMVALUE args[ 6 ] ;
  LINELIST *lastline ;
  LINELIST *startline, *nextline ;
  PDF_IMC_PARAMS *imc ;
  uint8 linetype ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;


  stack = ( & imc->pdfstack ) ;

  /* Read the first 4 items off the stack into the last four elems of args. */
  if ( !stack_get_numeric(stack, &args[2], 4) )
    return FALSE ;

  /* Check that there is a current point. */
  lastline = ( theIPathInfo( gstateptr )).lastline ;
  if ( ! lastline )
    return error_handler( NOCURRENTPOINT ) ;

  linetype = theILineType( lastline ) ;

  /* Need to remember where the Bezier starts.  In the simplest case
     it will just be added on to the end of the subpath.
   */
  startline = lastline;

  if( linetype == MYCLOSE )
  {
    /* In this case the lastline will be replaced by the Bezier, 
       so work out where the Bezier will start.
     */
    startline = (theIPathInfo( gstateptr )).lastpath->subpath ;
    nextline = startline->next ;

    HQASSERT( nextline != NULL, "Unexpected Null in pdfop_v" ) ;

    while( nextline != lastline )
    {
      startline = nextline ;
      nextline = startline->next ;
    }
  }


  /* In this operator x1, y1 is the same as the current
   * point. However, the current point is in device space, not the
   * required user space. As a work-around, x1, y1 are assigned random
   * values, enabling the call to gs_curveto to be made. After gs_curveto is
   * called x1, y1 can be assigned their proper values.
   */
  args[ 0 ] = 0 ;
  args[ 1 ] = 0 ;

  /* Add 'curve to' to graphics state */
  /* TRUE indicates curve ctrl pts are absolute. */
  if ( ! gs_curveto( TRUE, TRUE, args, & ( theIPathInfo( gstateptr ))))
    return FALSE ;

  HQASSERT( theIPathInfo( gstateptr).lastpath != NULL,
            "Unexpected Null in pdfop_v" ) ;

  if( linetype == CLOSEPATH )
  {
    /* In this case we are starting from the new Bezier */
    startline = (theIPathInfo( gstateptr )).lastpath->subpath ;
  }

  /* Replace dummy x1, y1 with device space current point. */
  HQASSERT( startline != NULL, "Unexpected Null in pdfop_v") ;
  
  nextline = startline->next ;
  HQASSERT( nextline != NULL, "Unexpected Null in pdfop_v" ) ;
  
  nextline->point.x = startline->point.x ;
  nextline->point.y = startline->point.y ;

  npop( 4 , stack ) ;

  return TRUE ;
}

int32 pdfop_y( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  SYSTEMVALUE args[ 6 ] ;
  PDF_IMC_PARAMS *imc ;
  
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  
  stack = ( & imc->pdfstack ) ;
  
  /* Read the first 4 items off the stack into the first four elems of args. */
  if ( !stack_get_numeric(stack, args, 4) )
    return FALSE ;

  /* Add x3, y3 point into the last two elems of args. */
  args[ 4 ] = args[ 2 ] ;
  args[ 5 ] = args[ 3 ] ;
  
  /* Add 'curve to' to graphics state */
  /* TRUE indicates curve ctrl pts are absolute. */
  if ( ! gs_curveto( TRUE, TRUE, args, & ( theIPathInfo( gstateptr ))))
    return FALSE ;
  
  npop( 4 , stack ) ;
  return TRUE ;
}

/* end of file pdfpseg.c */

/* Log stripped */
