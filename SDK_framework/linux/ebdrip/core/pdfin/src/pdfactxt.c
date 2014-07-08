/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfactxt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Contains routines for handling AcroForm text fields (ie. within Widget
 * annotations.
 */

#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "strfilt.h"
#include "gstate.h"
#include "namedef_.h"
#include "dicthash.h" /* fast_insert_hash */
#include "stacks.h"
#include "matrix.h"
#include "fontops.h"  /* for calc_font_height() */

#include "swpdf.h"
#include "pdfin.h"
#include "pdffont.h"
#include "pdftxt.h"
#include "pdfshow.h"

#include "pdfactxt.h"

/**
 * pdf_WidgetCalcFontSize()
 * This routine is only called for single-line fields (i.e. not multi-line).
 * If the default appearance string (/DA) has defined a "/Fontname 0 Tf"
 * operation, the font needs to be scaled so that the given text nicely
 * fills the field either height-wise or width-wise.
*/
static Bool pdf_WidgetCalcFontSize( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pfd )
{
  OBJECT *pStrObj;
  OBJECT *pObj;
  SYSTEMVALUE xWidth = 0.0;
  SYSTEMVALUE yWidth = 0.0;
  SYSTEMVALUE FontHeight = 0.0;
  SYSTEMVALUE FieldWidth = 0.0;
  SYSTEMVALUE FieldHeight = 0.0;
  SYSTEMVALUE ScaleWidth = 0.0;
  SYSTEMVALUE ScaleHeight = 0.0;
  RECTANGLE rect = {0};
  SYSTEMVALUE savedTc, savedTw;
  int32 cmap_wmode = 0;   /* default writing mode 0 == horizontally. */

  PDF_IMC_PARAMS *imc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_IMC( imc );

  /* Provide font scale of unit */
  theIPDFFFontSize( gstateptr ) = 1.0;
  imc->textstate.newTM = TRUE;

  /* Get the text matrices worked out */
  if (!pdf_set_text_matrices( pdfc, pfd ))
    return FALSE;

  /* Before determining the unit string width, set possible Tc and Tw
  ** parameters to zero
  */
  savedTc = theIPDFFCharSpace( gstateptr );
  theIPDFFCharSpace( gstateptr ) = 0.0;
  savedTw = theIPDFFWordSpace( gstateptr );
  theIPDFFWordSpace( gstateptr ) = 0.0;


  /* Get the string width at "unit" font size */
  if (imc->pFieldValues->pValue != NULL)
    pStrObj = imc->pFieldValues->pValue;
  else
    pStrObj = imc->pFieldValues->pDefValue;

  HQASSERT( (pStrObj != NULL), "No string value in pdf_WidgetCalcFontSize" );
  HQASSERT( (oType(*pStrObj)==OSTRING), "Value not a string in pdf_WidgetCalcFontSize" );

  if (!pdf_stringwidth( pdfc, pfd, pStrObj, &xWidth, &yWidth, NULL, NULL ))
    return FALSE;

  /* Restore Tc and Tw parameters */
  theIPDFFCharSpace( gstateptr ) = savedTc;
  theIPDFFWordSpace( gstateptr ) = savedTw;

  /* Determine horizontal or vertical writing mode */
  if (pfd->cmap_details != NULL)
    cmap_wmode = pfd->cmap_details->wmode;

  /* Calculate the overall height of the font (at unit scale) */
  if (!calc_font_height( &FontHeight ))
    return FALSE;

  /* Obtain the height & width of the widget field */
  pObj = oArray( *(imc->pFieldRect) );
  rect.x = object_numeric_value( pObj++ );
  rect.y = object_numeric_value( pObj++ );
  rect.w = object_numeric_value( pObj++ );
  rect.h = object_numeric_value( pObj++ );
  FieldWidth  = rect.w;
  FieldHeight = rect.h;

  /* Determine scales based on what fits in width-ways
  ** and what fits in height ways, then take the smaller.
  */
  if (cmap_wmode == 0) {  /* Horizontal */
    if (xWidth != 0.0)
      ScaleWidth = FieldWidth / xWidth;
  }
  else {
    if (yWidth != 0.0)
      ScaleWidth = FieldHeight / yWidth;
  }

  if (FontHeight != 0.0) {
    if (cmap_wmode == 0)  /* Horizontal */
      ScaleHeight = FieldHeight / FontHeight;
    else
      ScaleHeight = FieldWidth / FontHeight;
  }

  HQASSERT( ((ScaleWidth != 0.0) && (ScaleHeight != 0.0)),
            "Bad string width/height in pdf_WidgetCalcFontSize" );

  if (ScaleWidth < ScaleHeight)
    theIPDFFFontSize( gstateptr ) = ScaleWidth;
  else
    theIPDFFFontSize( gstateptr ) = ScaleHeight;

  imc->textstate.newTM = TRUE;

  return TRUE;
}

/**
 * Function:  pdf_ApproxFontAttrs()   -- approximate font attributes
 *
 * If pdf_GetFontAttrs() fails to obtain the font bounding box (or
 * whatever), then this function is called to provide approximate figures.
 * The returned parameters are:-
 * 1.  pFontHeight - total height of the font's bounding box (or equivalent);
 * 2.  pLsb        - Left side bearing distance (ie. left margin of a glyph);
 * 3.  pYDrop      - see comment below.
*/
static Bool pdf_ApproxFontAttrs( PDF_IXC_PARAMS *ixc,
                                  SYSTEMVALUE *pFontHeight,
                                  SYSTEMVALUE *pLsb,
                                  SYSTEMVALUE *pYDrop )
{
  /* Calculate the overall height of the font  */
  if (!calc_font_height( pFontHeight ))
    return FALSE;

  /* The "yDrop" is the distance from the origin of a glyph to its vertical
  ** centre.  This is calculated as half the font's height minus the descender.
  ** Since we don't have the descender's length here, a guestimate will have
  ** to do - the 'TextFieldDefVertDisplacement' is provided as a percentage/100.
  */
  *pYDrop = *pFontHeight * ixc->AnnotParams.AcroFormParams.TextFieldDefVertDisplacement;

  /* Return left side brearing as zero */
  *pLsb = 0.0;

  return TRUE;
}

/**
 * Function:  pdf_GetFontAttrs()    -- get font attributes
 *
 * Obtains certain metrics from the current font in order to provide figures
 * necessary for positioning the text at the right place in the field.
 * The returned parameters are:-
 * 1.  pFontHeight - total height of the font's bounding box (or equivalent);
 * 2.  pLsb        - Left side bearing distance (ie. left margin of a glyph);
 * 3.  pYDrop      - Distance from origin to vertical centre of glyph.
*/
static Bool pdf_GetFontAttrs( PDF_IXC_PARAMS *ixc,
                               SYSTEMVALUE *pFontHeight,
                               SYSTEMVALUE *pLsb,
                               SYSTEMVALUE *pYDrop )
{
  OBJECT *pBBox;


  /* Obtain the font's bounding box. */
  pBBox = theFontInfo(*gstateptr).fontbbox;
  if (pBBox != NULL) {
    if (oType(*pBBox) == OARRAY || oType(*pBBox) == OPACKEDARRAY) {

      OBJECT *pObj = oArray(*pBBox);
      SYSTEMVALUE lsb, dsc, hgt;   /* left side bearing, descender, height above origin */

      lsb = object_numeric_value( &pObj[0] );
      dsc = object_numeric_value( &pObj[1] );
      hgt = object_numeric_value( &pObj[3] );
      *pFontHeight = hgt - dsc;    /* total font height */

      if (*pFontHeight > 0.0) {
        SYSTEMVALUE x, y;

        /*
        ** The 'y' displacement is the distance from the vertical centre
        ** of the font to the vertical origin.  This is calculated as
        ** half the font's height minus the descender
        */
        y = (*pFontHeight / 2.0) - fabs(dsc);

        /* The figure we have now is in the font's own character space units.
        ** Extract the FontMatrix in the currentfont and scale the metrics we need.
        */
        MATRIX_TRANSFORM_DXY( lsb, y, *pLsb, *pYDrop,
                              &theFontCompositeMatrix(theFontInfo(*gstateptr)) );
        MATRIX_TRANSFORM_DXY( 0, *pFontHeight, x, y,
                              &theFontCompositeMatrix(theFontInfo(*gstateptr)) );
        *pFontHeight = y;

        return TRUE;    /* Happy exit point */
      }
    }
  }

  /*
  ** To get here, either the font's bounding box was absent or it didn't
  ** yield a proper font height.
  */
  if (!pdf_ApproxFontAttrs( ixc, pFontHeight, pLsb, pYDrop ))
    return FALSE;

  return TRUE;
}


/**
 * pdf_CalcLinePosHoriz()   -- Horizontal writing mode.
 * Given the string width, calculates the x & y coords for where drawing the
 * string within the field rectangle should start.  Takes into consideration
 * whether or not "quadding" needs to be applied.
*/
static Bool pdf_CalcLinePosHoriz( PDF_IXC_PARAMS *ixc,
                                   int32 Quadding,
                                   RECTANGLE *pRect,
                                   SYSTEMVALUE StrWidth )
{
  SYSTEMVALUE x, y;
  SYSTEMVALUE FontHeight;
  SYSTEMVALUE Lsb, YDrop;

  /* Get info from the font's metrics */
  if (!pdf_GetFontAttrs( ixc, &FontHeight, &Lsb, &YDrop ))
    return FALSE;

  /*
  ** Calculate horizontal position of the text within the field.
  */
  x = pRect->x;
  switch (Quadding) {

  case ACROFORM_QUAD_RIGHT:   /* Right-justify: right edge of string to right of field */
    x += (pRect->w - StrWidth);
    break;

  case ACROFORM_QUAD_CENTRE:  /* Centre-justify: centre of string in centre of field */
    x += (pRect->w / 2.0);
    x -= (StrWidth / 2.0);
    break;

  case ACROFORM_QUAD_LEFT:    /* Left-justify */
  default:
    break;
  }

  /*
  ** The vertical position is based on centring the string within the box
  ** which entails knowing the font's descender (distance from font origin
  ** to bottom of its bounding box) in order to compensate for the fact
  ** that the font's y-origin is around 1/3 the height of the font.
  */
  y = pRect->y + ((pRect->h / 2.0) - YDrop);

  /* Return the x & y components through '*pRect' */
  pRect->x = x;
  pRect->y = y;

  return TRUE;
}


/**
 * pdf_CalcLinePosVert()    -- Vertical writing mode
 * Given the string width, calculates the x & y coords for where drawing
 * the string should start.  Takes into consideration whether or not "quadding"
 * needs to be applied.
*/
static void pdf_CalcLinePosVert( int32 Quadding, RECTANGLE *pRect,
                                  SYSTEMVALUE StrWidth )
{
  SYSTEMVALUE x, y;

  x = pRect->x + (pRect->w / 2.0);  /* Centre the text horizontally */

  /*
  ** Calculate vertical position of the text within the field.
  */
  switch (Quadding) {

  case ACROFORM_QUAD_RIGHT:   /* Right-justify: interpret as "bottom" justify */
    y = pRect->y + StrWidth;
    break;

  case ACROFORM_QUAD_CENTRE:  /* Centre-justify: centre of string in centre of field */
    y = pRect->y + (pRect->h / 2.0);
    y += (StrWidth / 2.0);
    break;

  case ACROFORM_QUAD_LEFT:    /* Left-justify (interpret as "top" justify) */
  default:
    y = pRect->y + pRect->h;        /* i.e. start at top of field */
    break;
  }

  /* Return the x & y components through '*pRect' */
  pRect->x = x;
  pRect->y = y;
}

/**
 * pdf_WidgetSetTm()
 * Set the text matrix for positioning the text within the Widget field.
*/
static Bool pdf_WidgetSetTm( PDFCONTEXT *pdfc, SYSTEMVALUE x, SYSTEMVALUE y )
{
  PDF_IMC_PARAMS *imc;
  OMATRIX matrix = { 0 };

  PDF_CHECK_MC( pdfc );
  PDF_GET_IMC( imc );

  matrix.matrix[0][0] = 1.0;
  matrix.matrix[1][0] = 0.0;
  matrix.matrix[0][1] = 0.0;
  matrix.matrix[1][1] = 1.0;
  matrix.matrix[2][0] = x;
  matrix.matrix[2][1] = y;
  MATRIX_SET_OPT_BOTH( &matrix );

  HQASSERT( matrix_assert( &matrix ), "not a proper matrix in pdf_WidgetSetTm" );

  pdf_setTm( imc, &matrix );

  return TRUE;
}

/**
 * This is the state structure used to retain information between successive
 * invocations of the call-back function "pdf_GlyphCallBack()".
*/
typedef struct {
  PDFCONTEXT      *pdfc;
  PDF_IMC_PARAMS  *imc;
  PDF_FONTDETAILS *pfd;

  RECTANGLE       *pRect;     /**< Bounding rectangle of the text field.  */
  int32            Testing;   /**< "testing" as opposed to actual output. */
  SYSTEMVALUE  SegWidth;
  SYSTEMVALUE  WidthToLastSpace;
  SYSTEMVALUE  WidthOfSpace;
  uint16  SegLenToLastSpace;
  OBJECT  Segment;
  SYSTEMVALUE CurPos;
  SYSTEMVALUE MaxLineSize;  /**< Height or Width of rect depending on writing mode. */

  int32 wmode;                 /**< Default writing mode = 0 = horizontal. */
  SYSTEMVALUE TL;              /**< Text leading (distance between lines). */
  SYSTEMVALUE clsp;            /**< Current Line Start Position.           */

} PDF_GLYPH_CB_STATE;


/**
 * pdf_WidgetOutputLine()
 * Given a string of text, outputs it as a single line being careful to
 * position it properly in the multi-line text field.  This function is
 * passed the "glyph state" structure that pdf_GlyphCallBack() uses since
 * that is where it is called from and uses many variables contained therein.
*/
static Bool pdf_WidgetOutputLine( PDF_GLYPH_CB_STATE *pS, SYSTEMVALUE StrWidth )
{
  SYSTEMVALUE x, y;


  /* Maintain 'Current line start position' */
  pS->clsp -= pS->TL;
  if (pS->Testing)              /* Don't output if only testing */
    return TRUE;


  if (pS->wmode == 0) {  /* Horizontal writing mode */

    /* Calculate horizontal position of the text within the field. */
    switch (pS->imc->pFieldValues->Quadding) {

    case ACROFORM_QUAD_RIGHT:   /* Right-justify: right edge of string to right of field */
      x = pS->pRect->x + pS->pRect->w - StrWidth;
      break;

    case ACROFORM_QUAD_CENTRE:  /* Centre-justify: centre of string in centre of field */
      x = pS->pRect->x + (pS->pRect->w / 2.0);
      x -= (StrWidth / 2.0);
      break;

    case ACROFORM_QUAD_LEFT:    /* Left-justify */
    default:
      x = pS->pRect->x;
      break;
    }

    /* The vertical position is already given as the Current Line Start Position (clsp). */
    y = pS->clsp;
  }
  else {  /* Vertical writing mode */

    /* Calculate vertical position of the text within the field. */
    switch (pS->imc->pFieldValues->Quadding) {

    case ACROFORM_QUAD_RIGHT:   /* Right-justify: interpret as "bottom" justify */
      y = pS->pRect->y + StrWidth;
      break;

    case ACROFORM_QUAD_CENTRE:  /* Centre-justify: centre of string in centre of field */
      y = pS->pRect->y + (pS->pRect->h / 2.0);
      y += (StrWidth / 2.0);
      break;

    case ACROFORM_QUAD_LEFT:    /* Left-justify (interpret as "top" justify) */
    default:
      y = pS->pRect->y + pS->pRect->h;        /* i.e. start at top of field */
      break;
    }

    /* The horizontal position is already given as the Current Line Start Position (clsp). */
    x = pS->clsp;
  }

  /* Set the Text Matrix to position our text.  */
  if (!pdf_WidgetSetTm( pS->pdfc, x, y ))
    return FALSE;

  /* Show the text */
  if (!pdf_TjJ_showtext( pS->pdfc, pS->pfd, &pS->Segment ))
    return FALSE;

  if (!pdf_set_text_matrices( pS->pdfc, pS->pfd ))
    return FALSE;

  return TRUE;
}


/**
 * pdf_GlyphCallBack()
 * This function is invoked as a "call-back" from pdf_show()
 * (via the call to pdf_stringwidth() in pdf_WidgetMultiLine() below).
 * It is called after each character/glyph has been "plotchar'd" and its
 * metrics calculated.  Here, we are given 'GlyphPos' which is the current
 * (accumulating) displacement along the x or y direction according to
 * whatever the current writing mode is (horizontal or vertical).
 * By maintaining information in the state structure between successive calls
 * (for a single string), this function accumulates characters until the
 * current string segment that it is accumulating is too large to fit on one
 * line of the text field.  When that happens, the segment is split (depending
 * on where the last space character was, if any), is output, and a new
 * segment started for the next line.
*/

static Bool pdf_GlyphCallBack( void       *pvState,
                               SYSTEMVALUE GlyphPos,
                               char_selector_t *pSelector )
{
  PDF_GLYPH_CB_STATE *pS;
  int32  GlyphSelectorLen;
  int32  IsASpace;
  OMATRIX mtx;
  SYSTEMVALUE pos, dum;
  SYSTEMVALUE GlyphWidth;

  HQASSERT( pvState != NULL, "NULL pvState in pdf_GlyphCallBack" );

  /* Resolve the state pointer */
  pS = pvState;

  /* Translate the given "position" into user-space */
  if (!matrix_inverse( &theIgsPageCTM(gstateptr), &mtx ))
    return error_handler(UNDEFINEDRESULT);

  if (pS->wmode == 0)
    MATRIX_TRANSFORM_DXY( GlyphPos, 0.0, pos, dum, &mtx );
  else
    MATRIX_TRANSFORM_DXY( 0.0, GlyphPos, dum, pos, &mtx );

  GlyphWidth = fabs(pos) - pS->CurPos;
  pS->CurPos = fabs(pos);


  if (pSelector == NULL) {        /* End-of-string. No further glyph obtained. */
    if (pS->SegWidth > 0.0)       /* Any glyphs read in since last line-break? */
      if (!pdf_WidgetOutputLine( pS, pS->SegWidth ))  /* flush out */
        return FALSE;
  }
  else {                            /* We have another character to use */

    /* De-reference the required selector parameters */
    GlyphSelectorLen = theLen(pSelector->string); /* num bytes in string for this glyph */
    IsASpace = (pSelector->cid == ' ');

    if ((pS->SegWidth + GlyphWidth) < pS->MaxLineSize) {
      /* segment still fits in */
      if (IsASpace) {               /* So, record where last space was */
        pS->WidthToLastSpace  = pS->SegWidth;
        pS->SegLenToLastSpace = theLen(pS->Segment);
        pS->WidthOfSpace      = GlyphWidth;
      }

      /* Append glyph (space or not) to current segment */
      pS->SegWidth += GlyphWidth;
      theLen(pS->Segment) = CAST_TO_UINT16( theLen(pS->Segment) + GlyphSelectorLen );
    }
    else {                         /* Segment wouldn't fit */
      if (IsASpace) {
        /* The current line is filled upon a coincidental space character.
        ** The space itself is not output and is skipped.
        */
        if (!pdf_WidgetOutputLine( pS, pS->SegWidth ))
          return FALSE;

        /* Reset the segment pointer, etc. */
        pS->SegWidth = 0.0;
        pS->WidthToLastSpace = 0.0;
        pS->SegLenToLastSpace = 0;
        oString(pS->Segment) += (theLen(pS->Segment) + GlyphSelectorLen);
        theLen(pS->Segment) = 0;
      }
      else if (pS->SegLenToLastSpace == 0) {
        /* There's no "last space" so we've got a single word longer
        ** than the width of the field. Split arbitrarily within the
        ** word (that's what Adobe do!).
        */
        if (!pdf_WidgetOutputLine( pS, pS->SegWidth ))
          return FALSE;

        /* Reset the segment pointer, etc. */
        pS->SegWidth = GlyphWidth;
        oString(pS->Segment) += theLen(pS->Segment);
        theLen(pS->Segment) = CAST_TO_UINT16( GlyphSelectorLen );
      }
      else {
        /* Line is full but need to split on a word boundary.
        ** Go back to the last known space (which we know did fit in).
        */
        uint16 SegLen = theLen(pS->Segment);
        theLen(pS->Segment) = pS->SegLenToLastSpace;  /* Set for 'output' routine */

        if (!pdf_WidgetOutputLine( pS, pS->WidthToLastSpace ))
          return FALSE;

        /* Reset the segment pointer, etc. */
        pS->SegWidth -= pS->WidthToLastSpace;
        pS->SegWidth -= pS->WidthOfSpace;   /* 'cos the space needs to be ignored */
        pS->SegWidth += GlyphWidth;
        oString(pS->Segment) += (pS->SegLenToLastSpace + GlyphSelectorLen);
        theLen(pS->Segment) = CAST_TO_UINT16( SegLen - pS->SegLenToLastSpace );
        pS->WidthToLastSpace  = 0.0;
        pS->SegLenToLastSpace = 0;
      }
    }
  }

   /* until end-of-string */

  return TRUE;
}

/**
 * pdf_WidgetMultiLine()
 * This function handles multi-line text fields. It can be called in two
 * different ways: (a) to output the string directly; and (b) to only test
 * whether the string will fit in the field, not to actually output it.
 * This is governed by the 'pTesting' parameter: if given as TRUE (i.e.
 * to only test, not output) then it is also used to return the result of the
 * test.
 * The problem facing this function is how to divide up the string into
 * several lines.  Preference is given to trying to split the line up on
 * word boundaries (i.e. spaces).  If a word is too large, then the word
 * itself is split arbitrarily.
 * This function makes no assumptions about the current font. Because
 * strings may contain more than one byte per glyph (e.g. CID fonts), and
 * different font metrics might be involved (including residing settings for
 * the Tc & Tw operators), this function calls into pdf_stringwidth/pdf_show
 * supplying a call-back function - pdf_GlyphCallBack() - which is invoked
 * for each glyph in the string,  It is provided with the glyph's "width"
 * (sensitive to horizontal/vertical writing mode), etc., necessary for
 * managing the breaking up of the string into multiple lines.
*/
static Bool pdf_WidgetMultiLine( PDFCONTEXT *pdfc,
                                  PDF_FONTDETAILS *pfd,
                                  RECTANGLE  *pRect,  /* Text field rectangle    */
                                  OBJECT     *pStr,   /* Single string to output */
                                  int32      *pTesting )
{
  SYSTEMVALUE StrHWidth;
  SYSTEMVALUE StrVWidth;
  SYSTEMVALUE FontHeight, Lsb, YDrop;
  PDF_GLYPH_CB_STATE GlyphState;

  PDF_IMC_PARAMS *imc;
  PDFXCONTEXT    *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );
  PDF_GET_IMC( imc );

  GlyphState.pdfc = pdfc;
  GlyphState.imc  = imc;
  GlyphState.pfd  = pfd;
  GlyphState.pRect = pRect;

  GlyphState.Testing = FALSE;
  if (pTesting != NULL)
    GlyphState.Testing = *pTesting;

  if (theILen(pStr) == 0) /* Any zero-length string will fit! */
    return TRUE;

  /* Get the text matrices re-calculated */
  imc->textstate.newTM = TRUE;
  if (!pdf_set_text_matrices( pdfc, pfd ))
    return FALSE;

  /* A subsequent call to pdf_GetFontAttrs() requires that certain gstate
  ** parameters are set up  -  parameters which sometimes only get set up once
  ** 'plotchar()' has done its dooos.  So, 'pdf_stringwidth()' is called here anyway.
  */
  if (!pdf_stringwidth( pdfc, pfd, pStr, &StrHWidth, &StrVWidth, NULL, NULL ))
    return FALSE;

  /* Get the font's height, etc. */
  if (!pdf_GetFontAttrs( ixc, &FontHeight, &Lsb, &YDrop ))
    return FALSE;

  /* Determine horizontal or vertical writing mode */
  GlyphState.wmode = 0;       /* Default is horizontal */
  if (pfd->cmap_details != NULL)
    GlyphState.wmode = pfd->cmap_details->wmode;

  /* If the TL parameter has not been defined in the default appearance (DA)
  ** string, then calculate one.  Add to the font's height a fixed percentage.
  ** This will be our 'TL' metric which is the distance between successive lines of text.
  */
  if (theIPDFFLeading(gstateptr) != 0.0)
    GlyphState.TL = theIPDFFLeading(gstateptr);
  else
    GlyphState.TL = FontHeight + (FontHeight * ixc->AnnotParams.AcroFormParams.TextFieldFontLeading);

  /* Initialise a vertical "current line start position" (clsp) which is maintained
  ** immediately after each call to pdf_WidgetOutputLine().  The initial value
  ** is to place the line at the top of the field but on the text's baseline orgin.
  */
  if (GlyphState.wmode == 0) {
    GlyphState.clsp = pRect->y + pRect->h;
    GlyphState.clsp -= ((FontHeight / 2.0) + YDrop);
    GlyphState.clsp += GlyphState.TL;
    GlyphState.MaxLineSize = pRect->w;
  }
  else {
    GlyphState.clsp = pRect->x + (FontHeight / 2.0);
    GlyphState.clsp -= GlyphState.TL;
    GlyphState.TL = -1.0 * GlyphState.TL;
    GlyphState.MaxLineSize = pRect->h;
  }


  /* Initialise the rest of the 'GlyphState' structure and then call
  ** 'pdf_stringwidth()', which will in turn call pdf_show() resulting
  ** in our 'GlyphCallBack()' function being invoked for each character.
  */
  GlyphState.CurPos = 0.0;
  GlyphState.SegWidth  = 0.0;
  GlyphState.WidthOfSpace = 0.0;
  GlyphState.WidthToLastSpace = 0.0;
  GlyphState.SegLenToLastSpace = 0;
  GlyphState.Segment = onull ; /* Struct copy to set slot properties */
  Copy( &GlyphState.Segment, pStr );
  theLen(GlyphState.Segment) = 0;

  if (!pdf_stringwidth( pdfc, pfd, pStr, &StrHWidth, &StrVWidth, pdf_GlyphCallBack, &GlyphState ))
    return FALSE;

  /* Invoke the call-back directly one last time to flush out any remaining
  ** string segment.
  */
  if (!pdf_GlyphCallBack( &GlyphState, 0.0, NULL ))
    return FALSE;

  /* Return whether or not all the text fits within the field (for the sake
  ** of "testing" for auto-scaling the font size).  I.e. was the last line
  ** all visible above the bottom border of the field.
  */
  if (GlyphState.Testing) {
    SYSTEMVALUE LastLinePos;

    if (GlyphState.wmode == 0) {
      LastLinePos = pRect->y + (FontHeight / 2.0) - YDrop;
      *pTesting = (GlyphState.clsp >= LastLinePos);
    }
    else {
      LastLinePos = (pRect->x + pRect->w) - (FontHeight / 2.0);
      *pTesting = (GlyphState.clsp <= LastLinePos);
    }
  }

  return TRUE;
}


/**
 * pdf_WidgetPosText()
 * Locates a position within an Acroform text field such that a string (about
 * to the written in) will be placed vertically centred but left justified.
*/
static Bool pdf_WidgetPosText( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pfd )
{
  SYSTEMVALUE StrHWidth, StrVWidth;
  RECTANGLE rect = {0};
  OBJECT *pStr = NULL;
  OBJECT *pObj = NULL;
  int32 cmap_wmode = 0;   /* default writing mode 0 == horizontally. */
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;
  PDF_IMC_PARAMS *imc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_IMC( imc );
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );

  /* Check we have a string to output */
  if (imc->pFieldValues->pValue != NULL)
    pStr = imc->pFieldValues->pValue;
  else
    pStr = imc->pFieldValues->pDefValue;

  HQASSERT( (pStr != NULL), "No string value in pdf_WidgetPosText()" );
  HQASSERT( (oType(*pStr)==OSTRING), "Value not a string in pdf_WidgetPosText" );


  /* Obtain the height & width of the widget field */
  pObj = oArray( *(imc->pFieldRect) );
  rect.x = object_numeric_value( pObj++ );
  rect.y = object_numeric_value( pObj++ );
  rect.w = object_numeric_value( pObj++ );
  rect.h = object_numeric_value( pObj++ );


  /* Handle single/multi line fields separately */
  if ((imc->pFieldValues->FieldFlags & ACROFORM_FLAG_MULTILINE) == 0) {

    /* Get the text matrices re-calculated */
    if (!pdf_set_text_matrices( pdfc, pfd ))
      return FALSE;

    /* A subsequent call to pdf_GetFontAttrs() requires that certain gstate
    ** parameters are set up  -  parameters which sometimes only get set up
    ** once 'plotchar()' has done its dooos.  So, 'pdf_stringwidth()' is
    ** called here anyway.
    */
    if (!pdf_stringwidth( pdfc, pfd, pStr, &StrHWidth, &StrVWidth, NULL, NULL ))
      return FALSE;

    /* Determine horizontal or vertical writing mode */
    if (pfd->cmap_details != NULL)
      cmap_wmode = pfd->cmap_details->wmode;

    /* Calculate the position of the text on the line considering writing mode
    ** and quadding. The position is returned via the x & y components of 'rect'.
    */
    if (cmap_wmode == 0) {    /* Horizontal */
      if (!pdf_CalcLinePosHoriz( ixc, imc->pFieldValues->Quadding, &rect, StrHWidth ))
        return FALSE;
    }
    else {             /* Vertical writing mode */
      pdf_CalcLinePosVert( imc->pFieldValues->Quadding, &rect, StrVWidth );
    }

    if (!pdf_WidgetSetTm( pdfc, rect.x, rect.y ))
      return FALSE;

    if (!pdf_TjJ_showtext( pdfc, pfd, pStr ))
      return FALSE;
  }
  else {   /* is a multi-line field */

    /* If to auto-scale the font, keep trying different font sizes
    ** until all the text fits within the field.
    */
    if (theIPDFFFontSize( gstateptr ) == 0.0) {
      int32 TestFit = TRUE;
      SYSTEMVALUE Max =  ixc->AnnotParams.AcroFormParams.MultiLineMaxAutoFontScale;
      SYSTEMVALUE Min =  ixc->AnnotParams.AcroFormParams.MultiLineMinAutoFontScale;
      SYSTEMVALUE Incr = ixc->AnnotParams.AcroFormParams.MultiLineAutoFontScaleIncr;

      HQASSERT( (Incr > 0.001), "MultiLineAutoFontScaleIncr must be +ve and not too small" );
      HQASSERT( (Min > 0.5), "MultiLineMinAutoFontScale must be +ve and not too small" );
      HQASSERT( (Max > Min), "MultiLineMaxAutoFontScale must be greater than 'min'" );

      theIPDFFFontSize( gstateptr ) = Max;

      do {
        TestFit = TRUE;
        if (!pdf_WidgetMultiLine( pdfc, pfd, &rect, pStr, &TestFit ))
          return FALSE;

        if (!TestFit) /* Reduce the font size & try again */
          theIPDFFFontSize(gstateptr) -= (theIPDFFFontSize(gstateptr) * Incr);

      } while ((theIPDFFFontSize(gstateptr) > Min) && (!TestFit));

      if (theIPDFFFontSize( gstateptr ) < Min)
        theIPDFFFontSize( gstateptr ) = Min;

      if (!pdf_WidgetMultiLine( pdfc, pfd, &rect, pStr, NULL ))
        return FALSE;
    }
    else {   /* Not auto-scaled: use font directly */
      if (!pdf_WidgetMultiLine( pdfc, pfd, &rect, pStr, NULL ))
        return FALSE;
    }
  }

  imc->pFieldRect = NULL;  /* Don't call this routine any more */

  return TRUE;
}


/**
 * pdf_TextFieldAdjust()
 * This routine is called from pdfop_TjJ() just after the required font has
 * been selected, but before any text is about to be shown.  For Acroform text
 * fields, the requirements (implemented here) are to (a) select an appropriate
 * font size if the font size was previously given as zero (via the /DA string),
 * and (b) position the text within the text field.
*/
Bool pdf_TextFieldAdjust( PDFCONTEXT *pdfc, PDF_FONTDETAILS *pfd )
{
  PDF_IMC_PARAMS *imc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_IMC( imc );

  HQASSERT( ((imc->pFieldRect != NULL) && (imc->pFieldValues != NULL)),
            "Parameters not available to pdf_TextFieldAdjust()" );

  /*
  ** In the context of an AcroForm field, the Tf operator may have been (validly)
  ** given a font size of zero.  Normally, this is invalid, but in the context of
  ** an Acroform it means the font size must be selected so that the text to go
  ** in the field (almost) fills it.
  */
  if (theIPDFFFontSize(gstateptr) == 0.0) {
    if ((imc->pFieldValues->FieldFlags & ACROFORM_FLAG_MULTILINE) == 0) {
      if (!pdf_WidgetCalcFontSize( pdfc, pfd )) /* Only works for single line fields */
        return FALSE;

      HQASSERT( (theIPDFFFontSize(gstateptr) != 0.0),
                "Zero font-size for non-multiline field" );
    }
  }


  /* Position the text within the field. */
  if (!pdf_WidgetPosText( pdfc, pfd ))
    return FALSE;

  return TRUE;
}

/* Log stripped */
