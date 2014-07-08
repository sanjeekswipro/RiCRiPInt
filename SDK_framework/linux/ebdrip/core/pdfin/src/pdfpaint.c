/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfpaint.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF painting (fill, stroke) operators.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"

#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "gs_color.h"       /* GSC_FILL */
#include "gstate.h"
#include "gu_fills.h"
#include "stacks.h"
#include "pathcons.h"
#include "pathops.h"
#include "gu_path.h"
#include "system.h"
#include "clipops.h"
#include "vndetect.h"

#include "pdfexec.h"
#include "pdfclip.h"
#include "pdfpaint.h"
#include "pdfops.h"
#include "pdfin.h"

/* ---------------------------------------------------------------------- */

static Bool pdf_generic_fill( PDFCONTEXT *pdfc , int32 filltype )
{
  /* filltype: NZFILL_TYPE or EOFILL_TYPE
   */
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  /* If clipmode is set, must copy the path instead of stealing it. */
  if ( !dofill(&(theIPathInfo(gstateptr)), filltype, GSC_FILL,
        (imc->pdfclipmode != PDF_NO_CLIP) ? FILL_COPYCHARPATH : FILL_NORMAL) )
    return FALSE ;

  return pdf_check_clip( pdfc ) ;
}

/** The PDF operator f. */
Bool pdfop_f( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return pdf_generic_fill( pdfc , NZFILL_TYPE ) ;
}

/** The PDF operator f*. */
Bool pdfop_f1s( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return pdf_generic_fill( pdfc , EOFILL_TYPE ) ;
}

/** The PDF operator n. */
Bool pdfop_n( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return pdf_check_clip( pdfc ) ;
}

static Bool pdf_generic_s( PDFCONTEXT *pdfc , int32 closepath )
{
  STROKE_PARAMS params ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( closepath )
    if ( ! path_close( CLOSEPATH , & theIPathInfo( gstateptr )))
      return FALSE ;

  /* If clipmode is set, must copy the path instead of stealing it. */
  set_gstate_stroke( & params , & ( theIPathInfo( gstateptr )) , NULL ,
                     ( uint8 )( imc->pdfclipmode != PDF_NO_CLIP )) ;

  if ( ! dostroke( & params , GSC_STROKE , STROKE_NORMAL ))
    return FALSE ;

  return pdf_check_clip( pdfc ) ;
}

/** The PDF operator s. */
Bool pdfop_s( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  /* TRUE arg means do closepath */
  return pdf_generic_s( pdfc , TRUE ) ;
}

/** The PDF operator S. */
Bool pdfop_S( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  /* FALSE arg means do not do closepath */
  return pdf_generic_s( pdfc , FALSE ) ;
}

/** The PDF operator b. */
Bool pdfop_b( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return dostrokefill(&thePathInfo(*gstateptr), NULL, FALSE,
                      1.0, 1.0, IMPLICIT_GROUP_PDF,
                      TRUE /* closepath */ , NZFILL_TYPE ) &&
         pdf_check_clip( pdfc ) ;
}

/** The PDF operator b*. */
Bool pdfop_b1s( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return dostrokefill(&thePathInfo(*gstateptr), NULL, FALSE,
                      1.0, 1.0, IMPLICIT_GROUP_PDF,
                      TRUE /* closepath */ , EOFILL_TYPE ) &&
         pdf_check_clip( pdfc ) ;
}

/** The PDF operator B. */
Bool pdfop_B( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return dostrokefill(&thePathInfo(*gstateptr), NULL, FALSE,
                      1.0, 1.0, IMPLICIT_GROUP_PDF,
                      FALSE /* no closepath */ , NZFILL_TYPE ) &&
         pdf_check_clip( pdfc ) ;
}

/** The PDF operator B*. */
Bool pdfop_B1s( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return dostrokefill(&thePathInfo(*gstateptr), NULL, FALSE,
                      1.0, 1.0, IMPLICIT_GROUP_PDF,
                      FALSE /* no closepath */ , EOFILL_TYPE ) &&
         pdf_check_clip( pdfc ) ;
}

/* Log stripped */
