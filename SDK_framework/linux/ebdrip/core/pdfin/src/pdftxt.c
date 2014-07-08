/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdftxt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Text Object Operators
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"

#include "stacks.h"
#include "gstate.h"
#include "graphics.h"
#include "pathcons.h"
#include "fontops.h"
#include "clipops.h"
#include "utils.h"
#include "tranState.h"
#include "idlom.h"
#include "plotops.h"

#include "pdfexec.h"
#include "pdfops.h"
#include "pdfattrs.h"
#include "pdfres.h"
#include "pdfencod.h"
#include "pdffont.h"
#include "pdfshow.h"
#include "pdfmem.h"
#include "pdfin.h"
#include "pdftxt.h"

#include "pdfactxt.h"   /* pdf_TextFieldAdjust() */


/*
---------------------
Text Object Operators
---------------------
*/

/* -------------------------------------------------------------------------- */
void pdf_inittextstate( void )
{
  /* Tc */
  theIPDFFCharSpace( gstateptr ) = 0.0 ;
  /* Tf */
  theIPDFFFontSize( gstateptr ) = 0.0 ;
  theTags( theIPDFFFont( gstateptr )) = ONULL ;
  /* TL */
  theIPDFFLeading( gstateptr ) = 0.0 ;
  /* Tr */
  theIPDFFRenderMode( gstateptr ) = 0 ;
  /* Ts */
  theIPDFFRise( gstateptr ) = 0.0 ;
  /* Tw */
  theIPDFFWordSpace( gstateptr ) = 0.0 ;
  /* Tz */
  theIPDFFHorizScale( gstateptr ) = 1.0 ;
  /* Tk */
  tsSetTextKnockout(&gstateptr->tranState, TRUE) ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Begins a Text Object. Initialises the text matrix, Tm, and the line matrix,
 * Tlm, to the identity matrix.
 */
Bool pdfop_BT( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IMC_PARAMS *imc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_IXC( ixc ) ;

  if ( imc->textstate_count > 0 ) {
    PDF_TEXT_STATE *textstate ;

    /* Strictly speaking, we don't allow nested BT ops... */
    if ( ixc->strictpdf )
      return error_handler( UNDEFINEDRESULT ) ;

    textstate = ( PDF_TEXT_STATE *) mm_alloc( pdfxc->mm_structure_pool ,
					      sizeof( PDF_TEXT_STATE ) ,
					      MM_ALLOC_CLASS_PDF_TEXTSTATE ) ;
    if ( NULL == textstate )
      return error_handler( VMERROR ) ;

    /* Copy the current text state into the first node of the list. */
    *textstate = imc->textstate ;

    /* The new state becomes the first on the list of saved states. */
    imc->textstate.nextstate = textstate;
  }

  /* Initialise the current text state. */
  MATRIX_COPY(&imc->textstate.TLM, &identity_matrix) ;

  imc->textstate.newCP = TRUE ;
  imc->textstate.newTM = TRUE ;

  imc->textstate.used_clipmode = FALSE ;

  imc->textstate.savedHDLT = gstateptr->theHDLTinfo ;
  gstateptr->theHDLTinfo.next = &imc->textstate.savedHDLT ;
  if ( !IDLOM_BEGINTEXT(NAME_BT) )
    return FALSE ;

  ++(imc->textstate_count) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 *  Ends a Text Object. Discards the text matrix. */
Bool pdfop_ET( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IMC_PARAMS *imc ;
  Bool result = TRUE ;
#if defined( ASSERT_BUILD )
  PDF_IXC_PARAMS *ixc ;
#endif

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IMC( imc ) ;
#if defined( ASSERT_BUILD )
  PDF_GET_IXC( ixc ) ;
#endif

  if ( !IDLOM_ENDTEXT(NAME_BT, TRUE) )
    result = FALSE ;

  gstateptr->theHDLTinfo = imc->textstate.savedHDLT ;

  if ( imc->textstate_count > 1 ) {
    PDF_TEXT_STATE *textstate ;

    HQASSERT( ! ixc->strictpdf ,
	      "Somehow got a textstate_count > 1 with strictpdf in pdfop_ET." ) ;

    textstate = imc->textstate.nextstate ;
    HQASSERT(textstate, "Somehow lost saved text state") ;
    imc->textstate = *textstate ;

    /* Note: Errors between nested BT/ET will not cause memory leaks
     * as the text states are allocated from the pdf pool which is
     * released at the end of the job. */

    /* Free text state node now reinstated as the current text state. */
    mm_free(pdfxc->mm_structure_pool, textstate, sizeof(PDF_TEXT_STATE)) ;
  }
  else if ( imc->textstate_count < 1 ) {
    HQFAIL( "Mismatched BT/ET operators." ) ;
    return error_handler( UNDEFINEDRESULT ) ;
  }

  --(imc->textstate_count) ;

  if ( !result )
    return FALSE ;

  /* If we have used a clipping mode (=> to do this we must be in a clip
   * mode) then add the current path to the clipping path.
   * gs_addclip should be able to cope with an empty path (i.e. do nothing)
   */
  if ( imc->textstate.used_clipmode &&
       ! gs_addclip( NZFILL_TYPE, & ( theIPathInfo( gstateptr )), TRUE ))
    return FALSE ;

  return gs_newpath() ;
}

/** Clean up BT/ET pairs left open by a marking stream. This function takes
    care of HDLT callbacks, and closing Text targets. If there are any open
    BT/ET pairs, this function will return FALSE. */
Bool pdf_cleanup_bt(PDFCONTEXT *pdfc, Bool ok)
{
  Bool result = ok ;
  PDFXCONTEXT *pdfxc ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC(pdfc) ;
  PDF_GET_XC(pdfxc) ;
  PDF_GET_IMC(imc) ;

  /* Force HDLT callbacks for unclosed BT operators. */
  while ( imc->textstate_count > 0 ) {
    PDF_TEXT_STATE *textstate = &imc->textstate ;

    HQASSERT(gstateptr->theHDLTinfo.next == &textstate->savedHDLT,
             "BT saved HDLT state is not ancestor of gstate HDLT state") ;

    /* We try to give the HDLT targets whether they succeeded individually,
       regardless of the fact that we shouldn't need to clean up at all. */
    if ( !IDLOM_ENDTEXT(NAME_BT, ok) )
      result = FALSE ;

    gstateptr->theHDLTinfo = textstate->savedHDLT ;

    --imc->textstate_count ;
    if ( (textstate = textstate->nextstate) != NULL ) {
      imc->textstate = *textstate ;
      mm_free(pdfxc->mm_structure_pool, textstate, sizeof(PDF_TEXT_STATE)) ;
    }

    /* We shouldn't have any unclosed states. */
    if ( ok && !result )
      result = error_handler(SYNTAXERROR) ;
  }

  HQASSERT(gstateptr->theHDLTinfo.next == &gstackptr->theHDLTinfo,
           "Did not restore HDLT targets to sane state") ;

  return result ;
}

/*
--------------------
Text State Operators
--------------------
*/

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Set the text character spacing. */
Bool pdfop_Tc( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_Tc." ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( !object_get_numeric(theTop(*stack), &theIPDFFCharSpace(gstateptr)) )
    return FALSE ;

  npop( 1 , stack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Set the text font. */
Bool pdfop_Tf( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  OBJECT *theo ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_Tf." ) ;
  if ( theIStackSize( stack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  /* Get the size */

  if ( !object_get_numeric(theTop(*stack), &theIPDFFFontSize(gstateptr)) )
    return FALSE ;

  /* Now the font */

  theo = stackindex( 1 , stack ) ;

  if ( ! pdf_get_resourceid( pdfc , NAME_Font , theo , & theIPDFFFont( gstateptr )))
    return FALSE ;

  imc->textstate.newTM = TRUE ;

  npop( 2 , stack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Set the text leading.
 */
Bool pdfop_TL( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_TL." ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( !object_get_numeric(theTop(*stack), &theIPDFFLeading(gstateptr)) )
    return FALSE ;

  npop( 1 , stack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Set the text render mode.
 */
Bool pdfop_Tr( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  OBJECT *theo ;
  int32 mode ;
  PDFXCONTEXT *pdfxc ;
  PDF_IMC_PARAMS *imc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_IXC( ixc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_Tr." ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop(*stack) ;
  if ( oType( *theo ) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  mode = oInteger( *theo ) ;

  if ( mode < PDFRENDERMODE_MIN || mode > PDFRENDERMODE_MAX )
    return error_handler( RANGECHECK ) ;

  /* Only care if the mode is actually being changed. */
  if ( theIPDFFRenderMode( gstateptr ) != mode ) {

    /* If we are within a text object, and we've already (just) rendered any
    text in a clipping mode... */
    if ( imc->textstate_count > 0 && imc->textstate.used_clipmode ) {
      /* Strictly speaking, we don't allow text render mode to be
       * changed in a text object once a clip mode has been used. */
      if ( ixc->strictpdf )
        return error_handler( UNDEFINEDRESULT ) ;

      /* This is what Adobe do - if encounter a new render mode then
       * the previous path is added to the clip path (so that the
       * new text is subject to that clip path, and further stuff
       * will be subject to the intersection of both paths if the
       * new one is also a clip render mode)!
       */
      imc->textstate.used_clipmode = FALSE ;
      if ( !gs_addclip( NZFILL_TYPE, & theIPathInfo( gstateptr ) , TRUE ))
        return FALSE ;

      if ( !gs_newpath() )
        return FALSE ;

      /* If no other operator (e.g. Td) has reset the current point since
         the last show (Tj), then re-instantiate the current point in the
         text matrix since Tj only maintains the current point in imc->CP.
         (This only needs doing because gs_newpath() has just been called.)
      */
      if (!imc->textstate.newCP) {
        imc->textstate.TLM.matrix[ 2 ][ 0 ] = imc->textstate.CP.x;
        imc->textstate.TLM.matrix[ 2 ][ 1 ] = imc->textstate.CP.y;
        imc->textstate.newCP = TRUE ;
        imc->textstate.newTM = TRUE ;
      }
    }

    /* Allow mode change */
    theIPDFFRenderMode( gstateptr ) = mode ;
  }

  npop( 1 , stack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Set the text rise.
 */
Bool pdfop_Ts( PDFCONTEXT *pdfc )
{
  SYSTEMVALUE oldtrise ;
  SYSTEMVALUE newtrise ;
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_Ts." ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( !object_get_numeric(theTop(*stack), &newtrise) )
    return FALSE ;

  oldtrise = theIPDFFRise( gstateptr ) ;
  theIPDFFRise( gstateptr ) = newtrise ;

  /* If the rise has changed, then we need to factor it in to the current point. */
  if ( ! imc->textstate.newCP ) {
    SYSTEMVALUE chgtrise = newtrise - oldtrise ;
    if ( chgtrise != 0.0 ) {
      SYSTEMVALUE tx , ty ;
      MATRIX_TRANSFORM_DXY( 0.0, chgtrise, tx, ty, & imc->textstate.TLM ) ;
      MATRIX_TRANSFORM_DXY(  tx,       ty, tx, ty, & theIgsPageCTM( gstateptr )) ;
      imc->textstate.CP.x += tx ;
      imc->textstate.CP.y += ty ;
    }
  }

  imc->textstate.newTM = TRUE ;

  npop( 1 , stack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Set the text word spacing.
 */
Bool pdfop_Tw( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_Tw." ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( !object_get_numeric(theTop(*stack), &theIPDFFWordSpace(gstateptr)) )
    return FALSE ;

  npop( 1 , stack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Set the text horizontal scaling.
 */
Bool pdfop_Tz( PDFCONTEXT *pdfc )
{
  OBJECT *theo ;
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_Tz." ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop(*stack) ;
  if ( !object_get_numeric(theo, &theIPDFFHorizScale( gstateptr )) )
    return FALSE ;

  theIPDFFHorizScale( gstateptr ) /= 100.0 ;

  imc->textstate.newTM = TRUE ;

  npop( 1 , stack ) ;
  return TRUE ;
}

/*
--------------------------
Text Positioning Operators
--------------------------
*/

/* -------------------------------------------------------------------------- */
/** This 'sub-operator' conditionally does the work for Td, TD & T*. */
static Bool pdfop_TdTDT1s( PDFCONTEXT *pdfc ,
                            int32 useleading , int32 setleading )
{
  STACK *stack ;
  int32 stacksize ;
  SYSTEMVALUE tx , ty ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( ! useleading ) {
    stack = ( & imc->pdfstack ) ;
    stacksize = theIStackSize( stack ) ;

    /* Check stack has at least two operands. */
    if ( theIStackSize( stack ) < 1 )
      return error_handler( STACKUNDERFLOW ) ;

    /* Get top operand */
    if ( !object_get_numeric(stackindex(1, stack), &tx) ||
         !object_get_numeric(theTop(*stack), &ty) )
      return FALSE ;

    if ( setleading )
      theIPDFFLeading( gstateptr ) = -ty ;
  }
  else {
    stack = NULL ; /* To keep compiler quite. */
    HQASSERT( ! setleading , "setleading should be FALSE if useleading is TRUE" ) ;
    tx =  0.0 ;
    ty = -theIPDFFLeading( gstateptr ) ;
  }

  MATRIX_TRANSFORM_DXY( tx, ty, tx, ty, & imc->textstate.TLM ) ;
  imc->textstate.TLM.matrix[ 2 ][ 0 ] += tx ;
  imc->textstate.TLM.matrix[ 2 ][ 1 ] += ty ;

  imc->textstate.newCP = TRUE ;
  imc->textstate.newTM = TRUE ;

  if ( ! useleading )
    npop( 2 , stack ) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Moves to the start of the next line, offset from the start of the current
 * line by (tx,ty). More precisely, Td performs the following assignments:
 *            [  1  0  0 ]
 * Tm = Tlm = [  0  1  0 ] x Tlm
 *            [ tx ty  1 ]
 */
Bool pdfop_Td( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return pdfop_TdTDT1s( pdfc , FALSE , FALSE ) ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Moves to the start of the next line, offset from the start of the current
 * line by (tx,ty). As a side effect, this sets the leading parameter in the
 * text state.
 *   tx tx TD
 * is defined to have the same effect as
 *  -ty TL tx tx Td
 */
Bool pdfop_TD( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return pdfop_TdTDT1s( pdfc , FALSE , TRUE ) ;
}

/**
 * Sets the key variables on behalf pdfop_Tm (and also exported so the
 * text matrix can be set internally - e.g. by pdf_WidgetSetTm().)
 */
void pdf_setTm( PDF_IMC_PARAMS *imc, OMATRIX *matrix )
{
  HQASSERT( imc != NULL, "imc param null in pdf_setTM" );
  HQASSERT( matrix != NULL, "matrix param null in pdf_setTM" );

  imc->textstate.TLM = *matrix ;
  imc->textstate.newCP = TRUE ;
  imc->textstate.newTM = TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Sets the text matrix, Tm, and the text line matrix, Tlm. It also sets the
 * current point and line start position to the origin. Tm performs the following
 * assignments  :
 *            [ a b 0 ]
 * Tm = Tlm = [ c d 0 ]
 *            [ x y 1 ]
 */
Bool pdfop_Tm( PDFCONTEXT *pdfc )
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
    return error_handler( STACKUNDERFLOW ) ;

  for ( i = 5 ; i >= 0 ; i-- ) {
    /* Next operand */
    if ( !object_get_numeric(stackindex( 5 - i , stack ),
                             &matrix.matrix[ i >> 1 ][ i & 1 ]) )
      return FALSE ;
  }

  MATRIX_SET_OPT_BOTH( & matrix ) ;

  HQASSERT( matrix_assert( & matrix ) , "result not a proper optimised matrix" ) ;

  pdf_setTm( imc, &matrix );

  npop( 6 , stack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Moves to the start of the next line.
 *   T*
 * is defined to have the same effect as
 *   0 Tl Td
 * where Tl is the leading parameter of the text state.
 */
Bool pdfop_T1s( PDFCONTEXT *pdfc )
{
  return pdfop_TdTDT1s( pdfc , TRUE , FALSE ) ;
}


/**
 *  Sets up the various matrices used for text positioning and scaling in PDF.
 *  1.  The imc->TLM (Text Line Matrix) is initially set to the value of the
 *      "text matrix" (which transforms text space to user space).
 *  2.  The routine pdf_setTCJM() copies TLM into PQM but with the Trise and
 *      horizontal scaling factors added.  It then mults PQM by the CTM to
 *      provide the TCM which is used for additional character & word spacing.
 *      Next, the PQM is combined with the font scale (Tfs), and then
 *      multiplied by the CTM to provide TJM which is used by the Tj
 *      operator(s).
 *  3.  The routine pdf_setTRM() multiplies PQM by the CTM to yield TRM.
 *  4.  gs_setfontctm() stores the PQM in 'theFontATMTRM', thereby keeping
 *      the text/font transformation matrix for PDF separate. (Some of the
 *      postscript code "knows" this!)
 *  NB: This routine - pdf_set_text_matrices() - is exported so that it can
 *  be used by the AcroForm code (pdfwidge.c) which works in close conjunction
 *  with pdfop_TjJ() below.
 */
Bool pdf_set_text_matrices( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pdf_fontdetails )
{
  PDF_IMC_PARAMS *imc;
  PDF_TYPE3DETAILS *type3_details;

  PDF_GET_IMC( imc );

  /* Set text matrix */
  type3_details = pdf_fontdetails->type3_details;
  if (imc->textstate.newTM) {
    if (!pdf_setTCJM( pdfc, &imc->textstate.PQM ) )
       return FALSE;
    if (!pdf_setTRM( pdfc, &imc->textstate.PQM,
		     pdf_fontdetails->font_type == FONT_Type3 ?
		     type3_details->fntmatrix : NULL) )
	return FALSE;

    /* If we are dealing with an italic styled font then apply
     * a shear matrix to make it look 'about right' - as per Adobe
     * stuff.
     */
    if ((pdf_fontdetails->font_type != FONT_Type0) &&
        (pdf_fontdetails->font_type != FONT_Type3) &&
        (pdf_fontdetails->font_style & FSTYLE_Italic))
      matrix_mult( &italic_matrix, &imc->textstate.PQM, &imc->textstate.PQM );
  }

  /* Set the matrix to be used by the font. This includes
   * horizontal scaling and font size.
   */
  if (pdf_fontdetails->font_type != FONT_Type0)
    gs_setfontctm( &imc->textstate.PQM );

  return TRUE;
}

/**
 *  pdf_TjJ_showtext().
 *  This routine is only to be called in the context of pdfop_TjJ().
 *  It is only exported so it can be used from the AcroForm code (pdfwidge.c)
 *  which works in close conjunction with pdfop_TjJ().
 */
Bool pdf_TjJ_showtext( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pfd, OBJECT *pStr )
{
  Bool result ;
  int32 font_type ;
  PDF_TYPE3DETAILS *type3_details ;
  PDF_IMC_PARAMS *imc ;
  static OMATRIX ident_mat = { 1.0 , 0.0 , 0.0 , 1.0 , 0.0 , 0.0 ,
                               MATRIX_OPT_0011 };

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;


  /* set text matrices */
  if (!pdf_set_text_matrices( pdfc, pfd ))
    return FALSE;

  type3_details = pfd->type3_details ;
  font_type     = pfd->font_type ;

  if ((font_type == FONT_Type3) && (type3_details->resources)) {
    if ( ! pdf_add_resource( pdfc, type3_details->resources ))
      return FALSE ;
  }

  textContextEnter() ;
  /* detect 0 sized text via matrix optimization flags and skip it;
   * could use PQM instead. */
  result = imc->textstate.TRM.opt == 0 || theIPDFFHorizScale(gstateptr) == 0 ||
           pdf_show( pdfc, pfd, pStr, oType(*pStr), NULL ) ;
  textContextExit() ;

  /* Need to reset the font CTM back to identity, otherwise
   * further matrices will be concatenated with the one we
   * set in pdf_show
   */
  gs_setfontctm( &ident_mat ) ;

  /* If clipping then we have now used the current clip mode */
  if ( font_type != FONT_Type3 &&
       isPDFRenderModeClip( theIPDFFRenderMode( gstateptr )))
    imc->textstate.used_clipmode = TRUE ;

  if ( font_type == FONT_Type3 && type3_details->resources ) {
    pdf_remove_resource( pdfc ) ;
  }

  if ( ! result )
    return FALSE ;

  return TRUE ;
}


/*
---------------------
Text String Operators
---------------------
*/

/* -------------------------------------------------------------------------- */
/** pdfop_TjJ:
 * Accepts operand of either type1 or type2.
 * (To indicate that only one type is acceptable, pass it for both 1 and 2)
 */
static Bool pdfop_TjJ( PDFCONTEXT *pdfc , int32 type1 , int32 type2 )
{
  int32 type ;
  int32 font_type ;
  OBJECT *theo ;
  OBJECT *pdffont ;
  STACK *stack ;
  PDF_FONTDETAILS *pdf_fontdetails ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;


  stack = &imc->pdfstack;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = stackindex( 0 , stack ) ;
  type = oType( *theo ) ;
  if ( ( type != type1 ) && ( type != type2 ) )
    return error_handler( TYPECHECK ) ;


  /* Set the required font */
  pdffont =  &theIPDFFFont( gstateptr );
  if (oType( *pdffont ) == ONULL)
    return error_handler( UNDEFINEDRESULT );

  if ( !pdf_font_unpack( pdfc, pdffont, &pdf_fontdetails ) )
    return OPSTATE_ERROR ;

  HQASSERT( pdf_fontdetails, "pdf_fontdetails NULL after pdf_font_unpack in pdfop_TjT" ) ;

  font_type = pdf_fontdetails->font_type ;
  if ( font_type != FONT_Type0 && !gs_setfont( &(pdf_fontdetails->atmfont)) )
    return OPSTATE_ERROR ;


  /* For PDF AcroForm text fields, a number of adjustments may be needed
  ** before the text is shown.
  */
  if (imc->pFieldRect != NULL  &&  imc->pFieldValues != NULL) {
    if (!pdf_TextFieldAdjust( pdfc, pdf_fontdetails ))
      return FALSE;
  }
  else  {
    if (!pdf_TjJ_showtext( pdfc, pdf_fontdetails, theo ))
      return FALSE ;
  }

  npop( 1 , stack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Shows text string, using the character and word spacing parameters from the
 * text state.
 */
Bool pdfop_Tj( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return pdfop_TjJ( pdfc , OSTRING , OSTRING ) ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Moves to next line and shows text string, using the character and word spacing
 * parameters from the text state.
 *   string '
 * is defined to have the same effect as
 *   T* string Tj
 */
Bool pdfop_T1q( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return pdfop_T1s( pdfc ) && pdfop_Tj ( pdfc ) ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Moves to next line and shows text string. aw and ac are numbers expressed in
 * text space units. aw specifies the additional space width and ac specifies
 * the addition space between characters.
 *   aw ac string "
 * is defined to have the same effect as
 *   aw Tw ac Tc string '
 * Note the values specified by aq and ac remain the word and character spacings
 * after the " operator is executed.
 */
Bool pdfop_T2q( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_T2q." ) ;
  if ( theIStackSize( stack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( !object_get_numeric(stackindex(2, stack),
                           &theIPDFFWordSpace(gstateptr)) )
    return FALSE ;

  if ( !object_get_numeric(stackindex(1, stack),
                           &theIPDFFCharSpace(gstateptr)) )
    return FALSE ;

  if ( ! pdfop_T1q( pdfc ))
    return FALSE ;

  npop( 2 , stack ) ; /* pop 2 since pdfop_Tj called from pdfop_T1q will pop 1 */
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \ingroup pdfops
 * \brief
 * Shows text string, allowing individual character positioning, and using the
 * character and word spacing parameters from the text state.
 */
Bool pdfop_TJ( PDFCONTEXT *pdfc )
{
  PDF_CHECK_MC( pdfc ) ;

  return pdfop_TjJ( pdfc , OARRAY , OPACKEDARRAY ) ;
}


/* Log stripped */
