/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfannot.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Contains routines for handling an annotations within a PDF document.
 */

#include "core.h"

#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"
#include "constant.h"
#include "utils.h"

#include "matrix.h"
#include "routedev.h"
#include "graphics.h"
#include "gu_ctm.h"
#include "gstack.h"
#include "forms.h"
#include "stream.h"
#include "tranState.h"
#include "gstate.h"

#include "swpdf.h"
#include "pdfmatch.h"
#include "pdfxref.h"
#include "pdfdefs.h"
#include "pdfin.h"
#include "pdfmem.h"
#include "pdfwidge.h"
#include "pdfacrof.h"
#include "pdfx.h"

#include "pdfannot.h"

#if defined( DEBUG_BUILD )
static int32 pdf_annots_curr_objnum = 0; /* Very useful for debugging, I find. (ADH) */
#endif


/*
** The annotation dictionary: this one contains entries common to all
** annotation types as detailed in table 7.9 of the PDF manual 1.3, 2nd ed.
** At this level (ie. traversing the list of all annotations), we're not
** interested in most of the keys - they're examined within each individual
** annotation type's handler (e.g. see pdf_Widget() ).
*/
enum { E_acd_Subtype = 0, E_acd_Rect, E_acd_F, E_acd_AP,
       E_acd_AS, E_acd_CA, E_acd_OC, E_acd_FixedPrint,
       E_acd_max };

static NAMETYPEMATCH pdfannots_commondict[E_acd_max + 1] = {
/* .    { NAME_Type    | OOPTIONAL, 2, { ONAME, OINDIRECT }}, */
        { NAME_Subtype,             2, { ONAME, OINDIRECT }},
/* .    { NAME_Conents | OOPTIONAL, 2, { OSTRING, OINDIRECT }},  */
/* .    { NAME_P       | OOPTIONAL, 1, { OINDIRECT }},           */
        { NAME_Rect,                3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
/* .    { NAME_NM      | OOPTIONAL, 2, { OSTRING, OINDIRECT }},  */
/* .    { NAME_M       | OOPTIONAL, 2, { OSTRING, OINDIRECT }},  */
        { NAME_F       | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},
/* .    { NAME_BS      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},          */
/* .    { NAME_Border  | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }}, */
        { NAME_AP      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
        { NAME_AS      | OOPTIONAL, 2, { ONAME, OINDIRECT }},
/* .    { NAME_C       | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }}, */
        { NAME_CA      | OOPTIONAL, 3, { OINTEGER, OREAL, OINDIRECT }},
/* .    { NAME_T       | OOPTIONAL, 2, { OSTRING, OINDIRECT }},           */
/* .    { NAME_Popup   | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},       */
/* .    { NAME_A       | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
/* .    { NAME_AA      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
/* .    { NAME_StructParent | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
        { NAME_OC      | OOPTIONAL, 1, { OINDIRECT }}, /* optional content (PDF1.5) */
        { NAME_FixedPrint | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, /* watermark only (PDF1.6) */
          DUMMY_END_MATCH
};



/* -------------------------------------------------------------------------
** pdf_rectMatrix()
** Given the bounding box (of the annotation's appearance stream Form
** XObject) and the Rect (of the annotation itself), this function
** constructs a matrix representing the ratio of the Rect's rectangle to
** bounding box's rectangle.  The matrix is applied to the CTM.
** Note that this function ASSUMES that the gstate has been saved and
** will be restored (as the ctm is modified here).
*/
static Bool pdf_rectMatrix( OBJECT *pApDict,  /* Form XObject stream dict. */
                            OBJECT *pRect,    /* Annotation's Rect */
                            int32 *pDoDraw )
{
  OBJECT *pBBox;
  SYSTEMVALUE x_ratio, y_ratio;
  SYSTEMVALUE diff1, diff2;
  OMATRIX matrix = {0};
  sbbox_t rect_bbox;
  sbbox_t src_bbox;


  /* pDoDraw is so that we can return TRUE but signal that nothing should
     be drawn due to degenerate Rect or BBox. */
  HQASSERT( pDoDraw != NULL, "pDoDraw is null" );
  *pDoDraw = FALSE;

  /* Check the annotation Rect */
  HQASSERT( pRect != NULL, "Rect null in pdf_rectMatrix" );
  HQASSERT( oType(*pRect) == OARRAY, "Rect not an array in pdf_rectMatrix" );
  if ( !object_get_bbox(pRect, &rect_bbox) )
    return FALSE;

  /* Obtain the Form XObject's Bounding box. (NB: it really should
     be there!) */
  pBBox = fast_extract_hash_name( pApDict, NAME_BBox );
  if (pBBox == NULL) {                   /* /N not defined. */
    HQFAIL( "Bounding box not in Form XObject" );
    return error_handler(UNDEFINED);
  }

  HQASSERT( oType(*pBBox) == OARRAY, "BBox in Form XObject not an array" );
  if ( !object_get_bbox(pBBox, &src_bbox) )
    return FALSE;

  /* Work out the ratio on the x dimension as "width-of-rect / width-of-BBox". */
  diff1 = rect_bbox.x2 - rect_bbox.x1;
  diff2 = src_bbox.x2 - src_bbox.x1;

  if (diff1 <= EPSILON) {
    HQFAIL( "degenerate Rect (x) for Stamp annot." );
    return TRUE;
  }

  if (diff2 <= EPSILON) {
    HQFAIL( "degenerate BBox (x) for Stamp annot." );
    return TRUE;
  }

  x_ratio = diff1 / diff2;


  /* Work out the ratio on the y dimension as "height-of-rect / height-of-BBox". */
  diff1 = rect_bbox.y2 - rect_bbox.y1;
  diff2 = src_bbox.y2 - src_bbox.y1;

  if (diff1 <= EPSILON) {
    HQFAIL( "degenerate Rect (y) for Stamp annot." );
    return TRUE;
  }

  if (diff2 <= EPSILON) {
    HQFAIL( "degenerate BBox (y) for Stamp annot." );
    return TRUE;
  }

  y_ratio = diff1 / diff2;


  /* Put the ratio's into a nice matrix. */
  matrix.matrix[0][0] = x_ratio;
  matrix.matrix[1][0] = 0.0;
  matrix.matrix[0][1] = 0.0;
  matrix.matrix[1][1] = y_ratio;
  matrix.matrix[2][0] = 0.0;
  matrix.matrix[2][1] = 0.0;

  MATRIX_SET_OPT_BOTH( &matrix );
  HQASSERT( matrix_assert( &matrix ), "not proper matrix in Pdf_do_annots" );

  /* Set the CTM */
  gs_modifyctm( &matrix );

  *pDoDraw = TRUE;

  return TRUE;
}


/* -------------------------------------------------------------------------
** pdf_DefaultAnnots()
** Assuming the annotation dictionary has already been resolved via the
** pdfannots_commondict structure, this routine is called to render only
** the given normal appearance stream for the annotation. This can be done
** (almost) regardless of the type of annotation - they all provide appearance
** streams and if so, we simply do it.
*/
static Bool pdf_DefaultAnnots( PDFCONTEXT *pdfc, int32 AnnotType )
{
  OBJECT *pApDict;
  OBJECT *pApStrm;


  /* Use of this function should be restricted to the finite list of
     annotation types that it has been tested against. Against the PDF 1.4
     spec., this currently EXCLUDES Link, Movie and Popup annotation types.
     Popup annotations are excluded because they're only relevant to
     dynamic user interaction (popping up a window) which SW 'aint!
     Link and Movie annotations are excluded because they don't present
     a standard appearance stream.
     See table 8.14 in the 1.4 spec. */
  switch (AnnotType)
  {
  case NAME_Text:
  case NAME_FreeText:
  case NAME_Line:
  case NAME_Square:
  case NAME_Circle:
  case NAME_Highlight:
  case NAME_Underline:
  case NAME_Squiggly:
  case NAME_StrikeOut:
  case NAME_Stamp:
  case NAME_Ink:
  case NAME_FileAttachment:
  case NAME_Sound:
  case NAME_PrinterMark:
  case NAME_Polygon:
  case NAME_Polyline: /* The spec says lower case 'l'... */
  case NAME_PolyLine: /* ... but Acrobat 6 does upper case 'L' */
  case NAME_Caret:
  case NAME_Screen:
  case NAME_3D:
  case NAME_Watermark:
  case NAME_TrapNet:
    break;    /* ok, supported. */

  case NAME_Popup:    /* irrelevant */
  case NAME_Widget:   /* processed by acroforms */
    return TRUE;

  case NAME_Link:
  case NAME_Movie:
    HQTRACE( 1, ("Warning: link or movie annotation type not supported.") );
    return TRUE;

  default:
    HQTRACE( 1, ("Warning: unknown annotation type not supported.") );
    return TRUE;
  }

  /* Obtain the appearance dictionary given by the /AP key. If not present,
     no more to do. */
  pApDict = pdfannots_commondict[E_acd_AP].result;
  if (pApDict == NULL)
    return TRUE;

  /* All we care about is the /N entry ("normal" appearance).  The others
     are for interactive use only (i.e. mouse-rollover and mouse-down). */
  pApStrm = fast_extract_hash_name( pApDict, NAME_N );
  if (pApStrm == NULL)                    /* /N not defined. */
    return TRUE;

  if (oType(*pApStrm) == OINDIRECT)
    if (!pdf_lookupxref( pdfc, &pApStrm, oXRefID(*pApStrm),
                         theGen(*pApStrm), FALSE ))
      return FALSE;


  /* The /N appearance stream should be a stream.
     However, it can also be defined as a sub-dictionary in which case
     it lists a number of different "states" each with their own appearance.
     The actual state is given by the /AS entry in the annotation dictionary. */
  if (oType(*pApStrm) == ODICTIONARY) {
    OBJECT *pASobj = pdfannots_commondict[E_acd_AS].result;
    if (pASobj == NULL  ||  oType(*pASobj) != ONAME) {
      HQFAIL( "/AS state key invalid given /N dictionary" );
      return error_handler( TYPECHECK );
    }

    /* Resolve which appearance stream corresponds to the given state. */
    if (!pdf_ResolveAppStrm( pdfc, &pApStrm, pASobj ))
      return FALSE;
  }

  if (pApStrm != NULL) {
    if (oType(*pApStrm) != OFILE ) {
      HQFAIL( "/N appearance stream not resolved to a stream (/AS state?)" );
      return error_handler( TYPECHECK );
    }

    /* Get the dictionary associated with the stream. */
    pApDict = streamLookupDict( pApStrm );
    if (pApDict == NULL)
      return error_handler( UNDEFINED );

    HQASSERT( oType(*pApDict) == ODICTIONARY, "Appearance stream not a dictionary." );

    switch (AnnotType) {

    case NAME_PrinterMark:
      break;

    case NAME_3D:
    case NAME_Stamp:
    case NAME_Watermark:
      {
        /* For some annotations, the CTM needs scaling */
        Bool DoDraw = FALSE;
        if (!pdf_rectMatrix( pApDict, pdfannots_commondict[E_acd_Rect].result, &DoDraw ))
          return FALSE;

        if (!DoDraw)
          return TRUE;
      }

      /* fall through */

    default:

      /* If the CA key is present, we need to define default values for both
         CA and ca. Note that not all annotations types use this feature - see
         section 8.4.1 in the PDF 1.4 spec.
         AnnotType != NAME_PrinterMark */

      if ( pdfannots_commondict[E_acd_CA].result != NULL) {

        OBJECT *pObj = pdfannots_commondict[E_acd_CA].result;
        USERVALUE val = (USERVALUE) object_numeric_value( pObj );

        /* ca - non-stroking constant alpha */
        tsSetConstantAlpha( gsTranState(gstateptr), FALSE, val, gstateptr->colorInfo );

        /* CA - stroking constant alpha */
        tsSetConstantAlpha( gsTranState(gstateptr), TRUE, val, gstateptr->colorInfo );
      }
      break;
    }

    /* Execute the appearance stream */
    if (!pdf_SubmitField( pdfc, pApDict, pApStrm, NULL ))
      return FALSE;
  }

  return TRUE;
}

/* -------------------------------------------------------------------------
** pdf_ResolveAppStrm()
** Given the appearance state (e.g. a name like /On or /Off), the given
** dictionary (pApDict) is searched for that state.  If a match is found
** the retrieved object (which should be a form xobject stream) is
** returned.
** Note that even though the documentation doesn't say, multiple appearance
** streams dependent upon the "state" key (AS) are not relevant to text
** fields as text fields don't really have a state as such (unlike checkboxes).
** Set page 407 of PDF manual 1.3 (2nd ed.)
*/
Bool pdf_ResolveAppStrm( PDFCONTEXT *pdfc, OBJECT **ppApDict, OBJECT *pApState )
{
  OBJECT *pStrm, *pStrmI;

  /* Check the state */
  HQASSERT( pApState!=NULL, "State not given in pdf_ResolveAppRetStrm" );
  HQASSERT( oType(*pApState)==ONAME, "State not a Name in pdf_ResolveAppStrm" );

  pStrm = fast_extract_hash( *ppApDict, pApState );
  if (pStrm != NULL) {   /* NB: The absence of a match is not an error */
    /* If given as an indirect object, go get it. */
    if (oType(*pStrm) == OINDIRECT ) {
      if (!pdf_lookupxref( pdfc, &pStrmI, oXRefID(*pStrm),
                           theGen(*pStrm), FALSE )) {
        HQFAIL( "Couldn't get indirect State from appearance dictionary." );
        return FALSE;
      }

      if ((pStrmI == NULL) || (oType(*pStrmI) != OFILE))
        return error_handler( TYPECHECK );

      pStrm = pStrmI;
    }
  }

  /* Return the stream if any. */
  *ppApDict = pStrm;

  return TRUE;
}

/* ------------------------------------------------------------------
** pdf_SubmitField() is given the task of taking the Form XObject
** stream (which in turn was either taken directly from the PDF file
** or else constructed on the fly) representing the normal "appearance
** stream", and then submitting it for interpretation and rendering.
** Note that this function can be used to render appearance streams
** from both AcroForms (Widget annotations) and other annotations.  In
** the latter case, the parameter pDefRsrcs is given as null.
*/
Bool pdf_SubmitField( PDFCONTEXT *pdfc,
                      OBJECT *pApDict,     /* Appearance stream dict.  */
                      OBJECT *pApStrm,     /* Appearance stream stream */
                      OBJECT *pDefRsrcs )  /* More default resources   */
{
  /*
  ** The appearance stream (obtained from the /N key of the appearance
  ** dictionary above) is a Form XObject.  Here's its stream dictionary.
  */
  enum { apformdict_FormType, apformdict_BBox,
         apformdict_Matrix, apformdict_Resources,
         apformdict_dummy } ;
  static NAMETYPEMATCH appstrm_formdict[apformdict_dummy + 1] = {
  /* .    { NAME_Type      | OOPTIONAL,  1,  { ONAME }}, */
  /* .    { NAME_Subtype,                1,  { ONAME }}, */
      { NAME_FormType  | OOPTIONAL,  1,  { OINTEGER }},
      { NAME_BBox,                   3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
      { NAME_Matrix    | OOPTIONAL,  3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
      { NAME_Resources | OOPTIONAL,  2,  { ODICTIONARY, OINDIRECT }},
      DUMMY_END_MATCH
  } ;

  Bool ret ;
  OBJECT valobj = OBJECT_NOTVM_NOTHING;
  OBJECT nameobj = OBJECT_NOTVM_NOTHING;
  OBJECT localdict = OBJECT_NOTVM_NOTHING;
  OBJECT *pMtxObj = NULL;
  OBJECT IdentMtx = OBJECT_NOTVM_NOTHING;
  OBJECT *pResDict = NULL;
  int32 len = 5;    /* Length of local dictionary created here */
  PDF_IMC_PARAMS *imc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_IMC( imc );


  /* The Form XObject constituting the appearance stream needs untangling */
  if (!pdf_dictmatch( pdfc, pApDict, appstrm_formdict ))
    return FALSE;

  /* Create a local dictionary */
  if ( !pdf_create_dictionary( pdfc, len, &localdict ))
    return FALSE ;

  /* Insert "/FormType 1" */
  object_store_name(&nameobj,  NAME_FormType, LITERAL);
  object_store_integer(&valobj,  1 );

  ret = pdf_fast_insert_hash( pdfc, &localdict, &nameobj, &valobj );

  /* Insert bounding box value (which was not optional!) */
  if (ret) {
    object_store_name(&nameobj,  NAME_BBox, LITERAL);
    ret = pdf_fast_insert_hash( pdfc, &localdict, &nameobj, appstrm_formdict[apformdict_BBox].result );
  }

  /* Insert matrix value (which was optional). Default is the identity matrix. */
  if (ret) {
    object_store_name(&nameobj,  NAME_Matrix, LITERAL);
    pMtxObj = appstrm_formdict[apformdict_Matrix].result;

    if (pMtxObj == NULL) {
      ret = pdf_matrix( pdfc, &IdentMtx );
      if (ret)
        pMtxObj = &IdentMtx;
    }
  }

  if (ret) {
    ret = pdf_fast_insert_hash( pdfc, &localdict, &nameobj, pMtxObj );
  }

  /* Insert the stream itself as the 'PaintProc' */
  if (ret) {
    object_store_name(&nameobj,  NAME_PaintProc, LITERAL);
    ret = pdf_fast_insert_hash( pdfc, &localdict, &nameobj, pApStrm );
  }

  /* Resolve Resources dictionary.  Preference for one defined in the
     XObject (appearance stream), then for /DR (default resources) in the
     acroform field dictionaries (if present). If that failed, then I
     suppose we could default to the resources given by the page but
     according to the spec that should not be necessary. */
  if (ret) {
    pResDict = appstrm_formdict[apformdict_Resources].result;    /* Resources key */
    if (pResDict == NULL) {        /* Resources not defined in XObject dict.? */
      /* Use /DR (def. resources) from field dict. or even from AcroForm dict.*/
      pResDict = pDefRsrcs;
    }

    if (pResDict != NULL) {
      /* The resource dictionary is to be inserted not in our local dictionary
         (which is really just a set of parameters for 'gs_execform()'), but
         in the stream dictionary. */
      ret = pdf_copyobject( pdfc, pResDict, &valobj );
      if (ret) {
        object_store_name(&nameobj, NAME_Resources, LITERAL);
        ret = pdf_fast_insert_hash( pdfc, pApDict, &nameobj, &valobj );
      }
    }
  }

  /* Push our local dictionary onto the stack, then go execute the form */
  if (ret) {
    ret = push( &localdict, &imc->pdfstack );
    if (ret) {

      ret = gs_execform( pdfc->corecontext, &imc->pdfstack );

      pop(&imc->pdfstack);  /* Need to pop this, since gs_execform doesn't. */
    }
  }

  /* Clean up */
  pdf_destroy_dictionary( pdfc, len, &localdict );

  if (pMtxObj == &IdentMtx)
    pdf_destroy_array( pdfc, 6, pMtxObj );

  return ret;
}


/* -------------------------------------------------------------------------
** pdf_do_annots()
** From a particular page, traverses the list (array) of annotations on that
** page.  For each annotation, the '/Rect' key is located and the CTM
** modified to position the annotation.  The particular handler appropriate
** for the given type of annotation is then called.
*/

Bool pdf_do_annots( PDFCONTEXT *pdfc, OBJECT *list, int32 len )
{
  OBJECT *pDictObj = NULL;    /* Annotation dictionary */
  OBJECT *pAnnotType = NULL;

  int32 flags;
  Bool doPrint;
  OBJECT *pRect;
  OMATRIX matrix = {0};
  Bool ret, doit;

  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );


  /* Go through the list of annotations. */
  while (--len >= 0) {

    HQASSERT( (oType(*list) == ODICTIONARY) || (oType(*list) == OINDIRECT),
              "Annotations array entry incorrect type in Pdf_do_annots" );

    /* Get the annotation dictionary */
    /* Indirect references need resolving to the object itself */
    pDictObj = list;
    if (oType(*list) == OINDIRECT ) {

#if defined( DEBUG_BUILD )
      pdf_annots_curr_objnum = oXRefID(*list);
#endif
      if (!pdf_lookupxref( pdfc, &pDictObj, oXRefID(*list),
                           theGen(*list), FALSE ))
        return FALSE ;

      if ((pDictObj == NULL) || (oType(*pDictObj) != ODICTIONARY))
        return error_handler( TYPECHECK ) ;
    }

    /* We now have an annotation dictionary. Here we only need its subtype,
    ** Rectangle, and Flags.
    */
    if (!pdf_dictmatch( pdfc, pDictObj, pdfannots_commondict ))
      return FALSE;

    doit = TRUE;

    /* handle any optional content (OC) conditions */
    if (pdfannots_commondict[E_acd_OC].result != NULL) {
      enum { ocmatch_OC, ocmatch_dummy } ;
      static NAMETYPEMATCH ocmatch[ocmatch_dummy + 1] = {
        { NAME_OC      | OOPTIONAL, 1, { OINDIRECT }}, /* optional content (PDF1.5) */
          DUMMY_END_MATCH
      };
      OBJECT * id;

      /* get the indirect object */
      if (!dictmatch( pDictObj, ocmatch ))
        return FALSE;

      id = ocmatch[ocmatch_OC].result;

      if (!pdf_oc_getstate_OCG_or_OCMD(pdfc, id, & doit)) {
        return FALSE;
      }
    }

    /* if this annot is optionally "OFF" then get the next one */
    if (!doit || !optional_content_on) {
      list++;  /* Next annotation in the array */
      continue;
    }

    /* Check for PDF/X conformance. */
    if (! pdfxCheckAnnotation(pdfc, pDictObj))
      return FALSE;

    /* Determine whether or not we need to show this annotation.
    ** This depends on its 'flags' (see p.402, sect.7.4.2 of the PDF
    ** ref. manual, 1.3.) and the setting of 'AlwaysPrint' from the
    ** user's parameters.
    */
    doPrint = FALSE;    /* Default value for F entry is 0 */
    if (pdfannots_commondict[E_acd_F].result != NULL) {

      flags = oInteger( *(pdfannots_commondict[E_acd_F].result) );
      if ((((flags & ANNOT_FLAG_PRINT) != 0) || (ixc->AnnotParams.AlwaysPrint)) &&
           ((flags & ANNOT_FLAG_HIDDEN) == 0))
        doPrint = TRUE;
    }
    else if (ixc->AnnotParams.AlwaysPrint)
      doPrint = TRUE;

    /* Set things up to show the annotation */
    pAnnotType = pdfannots_commondict[E_acd_Subtype].result;    /* Subtype */

    /* For a TrapNet annotation, some extra PDF/X checks are required. */
    if ( oNameNumber(*pAnnotType) == NAME_TrapNet) {
      if (! pdfxCheckTrapNet(pdfc, pDictObj))
        return FALSE;
    }

    /* The Rect entry defines where on the page the annotation should
    ** be drawn. Use its first two numbers to provide the required
    ** translation of user space via the CTM.
    */
    pRect = pdfannots_commondict[E_acd_Rect].result;   /* Rect */
    HQASSERT( theLen(*pRect) == 4, "Invalid Rect for annotation in Pdf_do_annots" );

    if (!pdf_NormaliseRect( pRect ))    /* also makes all values OREALs */
      return FALSE;

    /* PDF/X imposes limits on the bounds of an annotation. */
    if (! pdfxCheckAnnotationBounds(pdfc, pAnnotType, pRect))
      return FALSE;

    if (doPrint) {
      GSTATE * temp_gstate;

      pRect = oArray(*pRect);
      matrix.matrix[0][0] = 1.0;
      matrix.matrix[1][0] = 0.0;
      matrix.matrix[0][1] = 0.0;
      matrix.matrix[1][1] = 1.0;
      matrix.matrix[2][0] = (SYSTEMVALUE) oReal(pRect[0]);
      matrix.matrix[2][1] = (SYSTEMVALUE) oReal(pRect[1]);

      MATRIX_SET_OPT_BOTH( &matrix );
      HQASSERT( matrix_assert( &matrix ), "not proper matrix in Pdf_do_annots" );

      /* save the graphics state and CTM */
      if (!gs_gpush( GST_SAVE ))
        return FALSE;

      temp_gstate = gstackptr;

      /* Act now according to the type (ie. subtype) of annotation we have.
         Ensure the gstate is always restored before exiting.*/
      ret = TRUE;
      switch ( oNameNumber(*pAnnotType) ) {

      case NAME_Widget:
        /* Set the CTM */
        gs_modifyctm( &matrix );
        if (ixc->AnnotParams.DoAcroForms)
          ret = pdf_Widget( pdfc, pDictObj );
        break;

      case NAME_Watermark:
        {
          OBJECT * FPdict = pdfannots_commondict[E_acd_FixedPrint].result;
          if (FPdict != NULL) {
            enum {
              FPdict_Type, FPdict_H, FPdict_V, FPdict_Matrix, FPdict_dummy
            } ;
            static NAMETYPEMATCH FPdict_match[FPdict_dummy + 1] = {
              { NAME_Type,  2,  { ONAME, OINDIRECT }},
              { NAME_H | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
              { NAME_V | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
              { NAME_Matrix    | OOPTIONAL,  3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
              DUMMY_END_MATCH
            } ;
            OBJECT * currobj;
            sbbox_t media_bbox;

            if (!pdf_dictmatch( pdfc, FPdict, FPdict_match ))
              return FALSE;

            if ( oNameNumber(*FPdict_match[FPdict_Type].result) != NAME_FixedPrint )
              return error_handler( SYNTAXERROR );

            currobj = FPdict_match[FPdict_Matrix].result;
            if (currobj) {
              if ( ! is_matrix( currobj , & matrix ))
                return FALSE ;
            }

            if (FPdict_match[FPdict_H].result || FPdict_match[FPdict_V].result) {
              SYSTEMVALUE w,h;
              OBJECT *pdevdict;
              OBJECT *mediaobj ;

              w = h = 0.0;

              /* /H and .V are the fractions of the size of the media */
              currobj = FPdict_match[FPdict_H].result;
              if (currobj) {
                w = object_numeric_value( currobj );
              }
              currobj = FPdict_match[FPdict_V].result;
              if (currobj) {
                h = object_numeric_value( currobj );
              }

              /* fetch the MediaBox */
              pdevdict = & theIgsDevicePageDict( gstateptr ) ;

              HQASSERT( oType(*pdevdict) == ODICTIONARY,
                        "pdevdict not a dictionary in pdf_do_annots" ) ;

              currobj = fast_extract_hash_name( pdevdict , NAME_PageSize ) ;
              HQASSERT( (currobj != NULL) && (oType(*currobj) == OARRAY),
                        "bad page size array in pdf_do_annots" ) ;
              media_bbox.x1 = 0;
              media_bbox.y1 = 0;
              media_bbox.x2 =  object_numeric_value( &oArray(*currobj)[0] );
              media_bbox.y2 =  object_numeric_value( &oArray(*currobj)[1] );

              /* get the crop box DICTIONARY */
              currobj = fast_extract_hash_name( pdevdict , NAME_CropBox ) ;
              if (currobj != NULL) {
                HQASSERT( oType(*currobj) == ODICTIONARY,
                        "bad crop box dict in pdf_do_annots" ) ;

                /* from that get the media box */
                mediaobj = fast_extract_hash_name( currobj , NAME_MediaBox ) ;
                HQASSERT( (mediaobj != NULL)||(oType(*mediaobj) != ONULL),
                          "bad mediabox in pdf_do_annots" ) ;

                if ((mediaobj != NULL) && (oType(*mediaobj) != ONULL)) {
                  media_bbox.x1 = object_numeric_value( &oArray(*mediaobj)[0] );
                  media_bbox.y1 = object_numeric_value( &oArray(*mediaobj)[1] );
                  media_bbox.x2 =  object_numeric_value( &oArray(*mediaobj)[2] );
                  media_bbox.y2 =  object_numeric_value( &oArray(*mediaobj)[3] );
                }
              }

              w *= media_bbox.x2 - media_bbox.x1;
              h *= media_bbox.y2 - media_bbox.y1;

              matrix.matrix[2][0] += w;
              matrix.matrix[2][1] += h;
            }
          }
        }
        /* fall through to default*/

      default:  /* provide default appearance stream rendering for all other types */
        /* Set the CTM */
        gs_modifyctm( &matrix );
        ret = pdf_DefaultAnnots( pdfc, oNameNumber(*pAnnotType) );
        break;
      }

      /* Restore graphics state & CTM.  (NB: gs_gpop() is inappropriate here!) */
      if (!gs_setgstate( temp_gstate, GST_GSAVE, FALSE, TRUE, FALSE, NULL ))
        return FALSE;

      if (!ret)       /* Check error return from specific annotation handler */
        return FALSE;
    }
    list++;  /* Next annotation in the array */
  }

  return TRUE;
}

void init_C_globals_pdfannot(void)
{
#if defined( DEBUG_BUILD )
  pdf_annots_curr_objnum = 0; /* Very useful for debugging, I find. (ADH) */
#endif
}

/* Log stripped */
