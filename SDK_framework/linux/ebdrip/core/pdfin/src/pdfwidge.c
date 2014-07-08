/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfwidge.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Contains routines to draw and fill AcroForm (i.e. Widget annotations) text
 * fields.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "strfilt.h"
#include "gstate.h"
#include "namedef_.h"
#include "dicthash.h" /* fast_insert_hash */
#include "stacks.h"
#include "hqmemcpy.h"
#include "swcopyf.h"
#include "stream.h"

#include "swpdf.h"
#include "pdfcntxt.h"
#include "pdfmatch.h"
#include "pdfxref.h"
#include "pdfin.h"
#include "pdfmem.h"
#include "pdffont.h"
#include "pdftxt.h"
#include "pdfshow.h"
#include "pdfacrof.h"
#include "pdfannot.h"

#include "pdfwidge.h"


/*
** The widget annotation dictionary: this one contains (a) entries common to
** all annotation types (table 7.9), and (b) entries specific to widget
** annotations (table 7.26).
*/
enum {
  Ewd_Rect = 0, Ewd_F, Ewd_Border, Ewd_BS, Ewd_AP, Ewd_AS, Ewd_MK, Ewd_dummy
};
static NAMETYPEMATCH pdfannot_widgetdict[Ewd_dummy + 1] = {
/* { NAME_Type    | OOPTIONAL, 2, { ONAME, OINDIRECT }},  -- it's an annotation   */
/* { NAME_Subtype,             2, { ONAME, OINDIRECT }},  -- it's a widget annot. */
/* { NAME_P       | OOPTIONAL, 1, { OINDIRECT }},         -- page - we know that. */
  { NAME_Rect,                3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
/* { NAME_M       | OOPTIONAL, 2, { OSTRING, OINDIRECT }},  */
  { NAME_F       | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},        /* Flags */
  { NAME_Border  | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  { NAME_BS      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},     /* Border style */
  { NAME_AP      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},     /* Appearance dict. */
  { NAME_AS      | OOPTIONAL, 2, { ONAME, OINDIRECT }},           /* Appearance state */
/* { NAME_C       | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }}, */
/* { NAME_T       | OOPTIONAL, 2, { OSTRING, OINDIRECT }},      */
/* { NAME_Popup   | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},  */
/* { NAME_A       | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},  */
/* { NAME_AA      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},  */
/* { NAME_StructParents | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */

        /* Entries specific to widget annotations only */
/* { NAME_H       | OOPTIONAL, 2, { ONAME, OINDIRECT }},  */
  { NAME_MK      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},  /* Appearance characteristics */
  DUMMY_END_MATCH
};


/*
** The annotation's "appearance dictionary" which is obtained from the /AP key
** in the annotation dictionary above. Note the /N key is listed as 'required'
** in table 7.12 of the PDF 1.3 manual (2nd ed.). This is the annotation's
** "normal" appearance.
*/
enum { pdfappear_N, pdfappear_dummy } ;
static NAMETYPEMATCH pdfappear_dict[pdfappear_dummy + 1] = {
  { NAME_N,              3, { OFILE, ODICTIONARY, OINDIRECT }},
/* { NAME_R  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
/* { NAME_D  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
  DUMMY_END_MATCH
} ;

/*
**  The appearance characteristics dictionary (given from the /MK entry
**  in the widget annotation dictionary).  See table 7.47 in the PDF
**  manual 1.3, 2nd ed.
*/
enum { Ead_R = 0, Ead_BC, Ead_BG, Ead_dummy };
static NAMETYPEMATCH pdf_appchardict[Ead_dummy + 1] = {
  { NAME_R   | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},              /* Rotation */
  { NAME_BC  | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},  /* Border color */
  { NAME_BG  | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},  /* Background color */
/* { NAME_CA  | OOPTIONAL, 2, { OSTRING, OINDIRECT }},     */
/* { NAME_RC  | OOPTIONAL, 2, { OSTRING, OINDIRECT }},     */
/* { NAME_AC  | OOPTIONAL, 2, { OSTRING, OINDIRECT }},     */
/* { NAME_I   | OOPTIONAL, 2, { OFILE, OINDIRECT }},       */
/* { NAME_RI  | OOPTIONAL, 2, { OFILE, OINDIRECT }},       */
/* { NAME_IX  | OOPTIONAL, 2, { OFILE, OINDIRECT }},       */
/* { NAME_IF  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
/* { NAME_TP  | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},    */
  DUMMY_END_MATCH
};

/*
**  The border style dictionary referenced by the /BS entry in the
**  widget annotation dictionary.  Table 7.11 in PDF 1.3, 2nd ed.
*/
enum { Ebd_W = 0, Ebd_S, Ebd_D, Ebd_dummy };
static NAMETYPEMATCH pdf_borderstyle_dict[Ebd_dummy + 1] = {
  { NAME_W   | OOPTIONAL, 3, { OINTEGER, OREAL, OINDIRECT }},      /* line width */
  { NAME_S   | OOPTIONAL, 2, { ONAME, OINDIRECT }},                /* line style */
  { NAME_D   | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }}, /* dash pattern */
  DUMMY_END_MATCH
};

/* This structure simplifies the parameters passed to all the
** output string handling routines.
*/
typedef struct {
  OBJECT Buffer;      /* Pointer to actual buffer */
  int32  Size;        /* Total buffer size allocated */
  PDFCONTEXT *pdfc;   /* Pain in butt */
} PDF_APPSTREAM_BUFF;

typedef Bool (*PDF_BEVEL_FUNCTION)( FPOINT *pts, OBJECT *pBBox, SYSTEMVALUE Width );


#define PDF_APPSTREAM_OUTSTRING_MINLEN  600
#define PDF_APPDICT_LEN 10



/* ---------------------------------------------------------------------------
** This function is given a (small) string to be appended to an output buffer
** (a big string) which it maintains.
** To use this function, commence by passing *pBuff as an object initialised
** to ONULL.  Then call this function repeatedly until the output buffer is
** complete.
*/
static Bool pdf_OutStr( PDF_APPSTREAM_BUFF *pBuff, char *pStr, uint16 len )
{
  uint8  *pDst;
  uint16 iStrLen = len;

  if (pStr == NULL)
    return TRUE;

  /* Get input string length if not provided */
  if (iStrLen <= 0)
    iStrLen = strlen_uint16( pStr );

  if (oType(pBuff->Buffer) == ONULL) {   /* Initialise */

    /* Create the output buffer.  Acrobat 5.0 tends to create appearance streams up
    ** to around 200 bytes.  In this case, SW is entirely in control of how
    ** much it's going to write into this buffer.  The main variable quantities
    ** are the lengths of the 'value' and 'default appearance' strings which have
    ** been passed via the 'pBuff->Size' parameter.  However, beveled edges can
    ** also require quite a bit of buffer space.
    */
    (pBuff->Size) += PDF_APPSTREAM_OUTSTRING_MINLEN; /* Return total buffer size */
    if (!pdf_create_string( pBuff->pdfc, pBuff->Size, &pBuff->Buffer ))
      return FALSE;

    theLen(pBuff->Buffer) = 0;  /* "used-up" string length is zero */
  }

  if ((theLen(pBuff->Buffer) + iStrLen) > pBuff->Size)  {
    /* Not enough room to append the next string. */
    HQFAIL( "Output buffer for appearance stream exceeded." );
    return error_handler( LIMITCHECK );
  }

  /* Append the string */
  pDst = oString(pBuff->Buffer) + theLen(pBuff->Buffer);
  HqMemCpy( pDst, pStr, (int32) iStrLen );

  /* Maintain current "used-up" length of string buffer */
  theLen(pBuff->Buffer) = CAST_TO_UINT16( theLen(pBuff->Buffer) + iStrLen );

  return TRUE;
}


/* ----------------------------------------------------------------------
** Given a real number, writes it out to the output buffer as a string.
*/
static Bool pdf_OutReal( PDF_APPSTREAM_BUFF *pBuff, SYSTEMVALUE rVal )
{
  uint8 tmpBuff[50];  /* More than enough */

  swcopyf( tmpBuff, (uint8 *) " %g", rVal );
  return pdf_OutStr( pBuff, (char*) tmpBuff, 0 );
}


/* ----------------------------------------------------------------------
** Given a number object, writes it out to the output buffer as a string.
*/
static Bool pdf_OutNum( PDF_APPSTREAM_BUFF *pBuff, OBJECT *pNumObj )
{
  int32 iNum;
  USERVALUE rNum;
  uint8 tmpBuff[50];  /* More than enough */

  switch (oType(*pNumObj)) {
  case OINTEGER:
    iNum = oInteger(*pNumObj);
    swcopyf( tmpBuff, (uint8 *) " %d", iNum );
    break;
  case OREAL:
    rNum = oReal(*pNumObj);
    swcopyf( tmpBuff, (uint8 *) " %f", rNum );
    break;
  default:
    HQFAIL( "Unexpected number type in pdf_OutNum" );
    return FALSE;
  }

  return pdf_OutStr( pBuff, (char*) tmpBuff, 0 );
}


/* ----------------------------------------------------------------------
** Given a number object, writes out its value with an addition. The
** additive is likely to be a real, so the output is that of a real.
*/
static Bool pdf_OutRealPlus( PDF_APPSTREAM_BUFF *pBuff, OBJECT *pNumObj, USERVALUE PlusBit )
{
  int32 iNum;
  USERVALUE rNum = 0.0f;

  switch (oType(*pNumObj)) {
  case OINTEGER:
    iNum = oInteger(*pNumObj);
    rNum = (USERVALUE) iNum + PlusBit;
    break;
  case OREAL:
    rNum = oReal(*pNumObj);
    rNum += PlusBit;
    break;
  default:
    HQFAIL( "Unexpected number type in pdf_OutNum" );
    return FALSE;
  }

  return pdf_OutReal( pBuff, (SYSTEMVALUE) rNum );
}


/* ----------------------------------------------------------------------
** Given an array object, writes out its values as "[ 1 2 3..]" .
*/
static Bool pdf_OutArray( PDF_APPSTREAM_BUFF *pBuff, OBJECT *pArrObj )
{
  int32 inx;
  OBJECT *pNum;

  if (!pdf_OutStr( pBuff, "[", 0 ))
    return FALSE;

  pNum = oArray(*pArrObj);
  for (inx = 0; inx < theLen(*pArrObj); inx++) {
    if (!pdf_OutNum( pBuff, pNum ))
      return FALSE;
    pNum++;
  }
  return pdf_OutStr( pBuff, " ] ", 0 );
}


/* ----------------------------------------------------------------------------
** Translates a 'rectangle', which is defined as bottom-left and top-right
** coordinates, into a Bounding Box in the appearance stream's coordinate
** system, which is defined as bottom-left coord and then width and height.
*/
static Bool pdfWidge_RectToBBox( OBJECT *pRect, OBJECT *pBBox )
{
  USERVALUE xy1, xy2 ;

  HQASSERT( pRect != NULL, "pRect NULL in pdfWidge_RectToBBox" );
  HQASSERT( pBBox != NULL, "pBBox NULL in pdfWidge_RectToBBox" );

  if (!pdf_NormaliseRect( pRect ))
    return FALSE;

  pBBox = oArray(*pBBox);   /* Dereference to array elements */
  pRect = oArray(*pRect);

  /* 1st 2 elements are zero, because they are in the Form space. */
  object_store_real(&pBBox[0], 0.0f) ;
  object_store_real(&pBBox[1], 0.0f) ;

  /* 3rd element is the width */
  if ( !object_get_real(&pRect[0], &xy1) ||
       !object_get_real(&pRect[2], &xy2) )
    return FALSE ;

  object_store_real(&pBBox[2], xy2 - xy1) ;

  /* 4th element is the height */
  if ( !object_get_real(&pRect[1], &xy1) ||
       !object_get_real(&pRect[3], &xy2) )
    return FALSE ;

  object_store_real(&pBBox[3], xy2 - xy1) ;

  return TRUE;
}


/* ------------------------------------------------------------------
** Given a number (len) of number-objects in an array, outputs the
** required color selection operator for the BG key in the
** appearance characteristics dictionary.
*/
static Bool pdf_SetBackgroundColor( PDF_APPSTREAM_BUFF *pBuff, uint16 len, OBJECT *pObj )
{
  if (len == 3) {  /* The array has 3 numbers, therefore RGB */
                   /* Make string:-  x x x rg */
    if (!pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutStr( pBuff, " rg", 0 ))
        return FALSE;
  }
  else if (len == 4) {  /* 4 numbers, therefore CMYK */
                        /* Make string:- x x x x k */
    if (!pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutStr( pBuff, " k", 0 ))
        return FALSE;
  }
  else if (len == 1) {  /* 1 number, therefore gray */
                        /* Make string:-  x g */
    if (!pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutStr( pBuff, " g", 0 ))
        return FALSE;
  }
  else {
    HQFAIL( "Invalid array length for BG appearance characteristic" );
    return error_handler( RANGECHECK );
  }

  return TRUE;
}


/* ------------------------------------------------------------------
** Given a number (len) of number-objects in an array, outputs the
** the required color selection operator for the BC key in the
** appearance characteristics dictionary.
** This function differs from pdf_SetBackgroundColor() in that the
** color selection operators are in capitals for 'stroke' rather than
** fill.
*/
static Bool pdf_SetBorderColor( PDF_APPSTREAM_BUFF *pBuff, uint16 len, OBJECT *pObj )
{
  if (len == 3) {   /* The array has 3 numbers, therefore RGB */
                    /* Make string:-  x x x rg */
    if (!pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutStr( pBuff, " RG", 0 ))
        return FALSE;
  }
  else if (len == 4) {  /* 4 numbers, therefore CMYK */
                        /* Make string:- x x x x k */
    if (!pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutStr( pBuff, " K", 0 ))
        return FALSE;
  }
  else if (len == 1) {  /* 1 number, therefore gray */
                        /* Make string:-  x g */
    if (!pdf_OutNum( pBuff, pObj++ ) ||
        !pdf_OutStr( pBuff, " G", 0 ))
        return FALSE;
  }
  else {
    HQFAIL( "Invalid array length for BC appearance characteristic" );
    return error_handler( RANGECHECK );
  }

  return TRUE;
}


/* ----------------------------------------------------------------------------
** pdf_SetShade()
** Given a color array (i.e. the border color), return a darker/lighter shade.
** That is, this routine writes out the color values and corresponding 'set
** color' operator to set a color.  The color itself is that of the border
** color (*pArr) plus or minus a difference (Diff).  If the colorspace
** (determined by the number of color components) is subtractive rather than
** additive (e.g. CMYK instead of RGB) then the 'diff' factor is negated.
*/
static Bool pdf_SetShade( PDF_APPSTREAM_BUFF *pBuff, OBJECT *pArr, SYSTEMVALUE Diff )
{
  int32 i;

  HQASSERT( pArr != NULL, "pArr null" );
  HQASSERT( oType(*pArr) == OARRAY, "Not an array" );
  HQASSERT( theLen(*pArr) <= 4, "Too many colors in border color" );

  /* If the colorspace (determined by the number of color components) is
     subtractive rather than additive (e.g. CMYK instead of RGB) then the
     'diff' factor is negated. */
  if (theLen(*pArr) == 4)
    Diff = -Diff;

  /* Write out the adjusted color values. */
  for (i=0; i < theLen(*pArr); i++) {
    OBJECT *pObj = &oArray(*pArr)[i];
    SYSTEMVALUE rVal = object_numeric_value( pObj ) + Diff;

    if (rVal > 1.0)
      rVal = 1.0;
    else if (rVal < 0.0)
      rVal = 0.0;

    if (!pdf_OutReal( pBuff, rVal ))
      return FALSE;
  }

  /* Select the appropriate 'set color' operator according to how many
     numbers were in the array. */
  switch (theLen(*pArr))
  {
  case 1:  /* 1 number, therefore gray */
    if (!pdf_OutStr( pBuff, " g", 0 ))
      return FALSE;
    break;
  case 3:   /* The array has 3 numbers, therefore RGB */
    if (!pdf_OutStr( pBuff, " rg", 0 ))
      return FALSE;
    break;
  case 4:   /* 4 numbers, therefore CMYK */
    if (!pdf_OutStr( pBuff, " k", 0 ))
      return FALSE;
    break;
  default:
    HQFAIL( "Invalid array length color shade" );
    return error_handler( RANGECHECK );
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdf_BevelPath()
** Circumscibes the beveled path.  If 'BevelWidth' is +ve, does the left and
** top edges; and if -ve, does the right and bottom edges.
*/
static void pdf_BevelPath( FPOINT *pts, OBJECT *pBBox, SYSTEMVALUE BevelWidth )
{
  SYSTEMVALUE Height;
  SYSTEMVALUE Width;

  if (BevelWidth > 0.0) {
    pts[0].x = object_numeric_value( &pBBox[0] );  /* Start at bottom-left */
    pts[0].y = object_numeric_value( &pBBox[1] );
    Width  = object_numeric_value( &pBBox[2] ) - pts[0].x ;
    Height = object_numeric_value( &pBBox[3] ) - pts[0].y ;
  } else {
    pts[0].x = object_numeric_value( &pBBox[2] ); /* Start at top-right */
    pts[0].y = object_numeric_value( &pBBox[3] );
    Width  = object_numeric_value( &pBBox[0] ) - pts[0].x ;
    Height = object_numeric_value( &pBBox[1] ) - pts[0].y ;
  }

  pts[1].x = pts[0].x;
  pts[1].y = pts[0].y + Height;

  pts[2].x = pts[1].x + Width;
  pts[2].y = pts[1].y;

  pts[3].x = pts[2].x - BevelWidth;
  pts[3].y = pts[2].y - BevelWidth;

  pts[4].x = pts[1].x + BevelWidth;
  pts[4].y = pts[3].y;

  pts[5].x = pts[4].x;
  pts[5].y = pts[0].y + BevelWidth;
}

/* ----------------------------------------------------------------------------
** pdf_DrawBevel()
** Draws either the right-and-bottom or left-and-top bevel edges of a rectangle
** (given by *pBBox). Each bevel is given a 'Width'.
*/
static Bool pdf_DrawBevel( PDF_APPSTREAM_BUFF *pBuff,
                           OBJECT *pBBox,
                           SYSTEMVALUE Width )
{
  int32 i;
  FPOINT pts[6];

  HQASSERT( pBBox != NULL, "pBBox null" );
  HQASSERT( oType(*pBBox) == OARRAY, "Not an array" );
  HQASSERT( theLen(*pBBox) == 4, "Bounding box invalid size" );

  /* Draw the required section of the bevel. */
  pdf_BevelPath( pts, oArray(*pBBox), Width );

  /* Moveto the first point */
  if (!pdf_OutReal( pBuff, pts[0].x ))
    return FALSE;
  if (!pdf_OutReal( pBuff, pts[0].y ))
    return FALSE;
  if (!pdf_OutStr( pBuff, " m", 0 ))
    return FALSE;

  /* Lineto the rest */
  for (i=1; i < NUM_ARRAY_ITEMS(pts); i++ ) {
    if (!pdf_OutReal( pBuff, pts[i].x ))
      return FALSE;
    if (!pdf_OutReal( pBuff, pts[i].y ))
      return FALSE;
    if (!pdf_OutStr( pBuff, " l", 0 ))
      return FALSE;
  }

  if (!pdf_OutStr( pBuff, " h f\r\n", 0 ))   /* Closepath & Fill it */
    return FALSE;

  return TRUE;
}

/* ----------------------------------------------------------------------------
** Drawing the field's border (variable line thickness and color).
** If there's an appearance characteristics (/MK) dictionary defined, and
** if it specifies a border color for the text field (/BC), then draw
** a rectangle just within the bounding box and stroke it with the color.
** The line width of the border is given by the W key in the BS dictionary
** available from the BS key in the widget dictionary.
*/
static Bool pdf_SetBorder( PDFCONTEXT *pdfc,
                           PDF_APPSTREAM_BUFF *pBuff,
                           OBJECT *pBBox,
                           USERVALUE *pBorderWidth,
                           OBJECT *pBG )
{
  USERVALUE Width;
  USERVALUE HalfWidth;
  OBJECT *pBsDict;
  OBJECT *pObj;
  OBJECT *pBC = NULL;
  int16  iStyle;
  OBJECT *pDashArray;
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );


  *pBorderWidth = 0.0;   /* Initialise return of whether or not border drawn */

  Width = (USERVALUE) ixc->AnnotParams.DefaultBorderWidth;  /* Provide default */
  iStyle = NAME_S;        /* Default border style is solid */
  pDashArray = NULL;

  /* Get the line width (& other attributes) for the border */
  pBsDict = pdfannot_widgetdict[Ewd_BS].result;      /* BS key */
  if (pBsDict != NULL) {

    if (!pdf_dictmatch( pdfc, pBsDict, pdf_borderstyle_dict ))
      return FALSE;

    if (pdf_borderstyle_dict[Ebd_W].result != NULL)  /* W key */
      Width = (USERVALUE) object_numeric_value( pdf_borderstyle_dict[Ebd_W].result );

    pObj = pdf_borderstyle_dict[Ebd_S].result;        /* S key (style) */
    if (pObj != NULL)
      iStyle = theINameNumber( oName(*pObj) );

    pDashArray = pdf_borderstyle_dict[Ebd_D].result;  /* D key (dash array) */
  }

  if (Width > 0.0f) {   /* zero width means "no border" */
    /* !!! Tecky Note:-  Discrepancy with spec.
    ** Acrobat 5.0 doesn't seem to draw a border unless the MK dict. exists.
    */
    if (pdfannot_widgetdict[Ewd_MK].result != NULL) {    /* MK key */

      pBC = pdf_appchardict[Ead_BC].result;              /* the BC array */
      if (pBC == NULL)
        pBC = pBG;    /* Default to the background color */

      if ((pBC != NULL) && (theLen(*pBC) > 0)) {
        if (!pdf_SetBorderColor( pBuff, theLen(*pBC), oArray(*pBC) ))
          return FALSE;

        /* For all border styles except Underline, draw a rectangle located
        ** within the bounding box (i.e. minus half the line width).
        */
        HalfWidth = Width / 2.0f;

        /* Set the line width. */
        if (!pdf_OutReal( pBuff, (SYSTEMVALUE) Width ) ||
            !pdf_OutStr( pBuff, " w ", 0 ))
          return FALSE;


        switch (iStyle) {

        case NAME_U:   /* Underline: Draw simple line on bottom of rectangle */
          pObj = oArray(*pBBox);  /* Point to 1st num of BBox */
          if (!pdf_OutNum( pBuff, &pObj[0] ) ||                 /* left-x    */
              !pdf_OutRealPlus( pBuff, &pObj[1], HalfWidth ) || /* bottom-y  */
              !pdf_OutStr( pBuff, " m ", 0 ) ||                 /* Move to it*/
              !pdf_OutNum( pBuff, &pObj[2] ) ||                 /* right-x   */
              !pdf_OutRealPlus( pBuff, &pObj[1], HalfWidth ) || /* bottom-y  */
              !pdf_OutStr( pBuff, " l s\r\n", 0 ))              /* Line to & stroke it.*/
              return FALSE;

          break;

        case NAME_B:    /* Beveled */
          if (!pdf_SetShade( pBuff, pBC, 0.5 ))
            return FALSE;
          if (!pdf_DrawBevel( pBuff, pBBox, (SYSTEMVALUE) Width ))
            return FALSE;
          if (!pdf_SetShade( pBuff, pBC, -0.5 ))
            return FALSE;
          if (!pdf_DrawBevel( pBuff, pBBox, (SYSTEMVALUE) Width * -1.0 ))
            return FALSE;
          break;

        case NAME_I:    /* Inset   */
          if (!pdf_SetShade( pBuff, pBC, -0.5 ))
            return FALSE;
          if (!pdf_DrawBevel( pBuff, pBBox, (SYSTEMVALUE) Width ))
            return FALSE;
          if (!pdf_SetShade( pBuff, pBC, 0.5 ))
            return FALSE;
          if (!pdf_DrawBevel( pBuff, pBBox, (SYSTEMVALUE) Width * -1.0 ))
            return FALSE;
          break;

        /* Dash pattern: set the 'd' operator to declare the 'setdash' pattern.*/
        case NAME_D:
          if (pDashArray != NULL) {
            if (!pdf_OutArray( pBuff, pDashArray ))
              return FALSE;
          } else {      /* default value is [3] */
            if (!pdf_OutStr( pBuff, " [3]", 0 ))
              return FALSE;
          }

          oInteger(inewobj) = 0;
          if (!pdf_OutNum( pBuff, &inewobj ) ||   /* Phase is always zero */
              !pdf_OutStr( pBuff, " d ", 0 ))
            return FALSE;

          /* ** FALL THROUGH **  The Dash still needs the rectangle path */

        case NAME_S:    /* Solid   */
        default:        /* See note 56 in Appendix H, PDF manual 1.3, 2nd ed.*/
          pObj = oArray(*pBBox);  /* Point to 1st num of BBox */
          if (!pdf_OutRealPlus( pBuff, pObj++, +HalfWidth ) || /* left-x  */
              !pdf_OutRealPlus( pBuff, pObj++, +HalfWidth ) || /* bottom-y*/
              !pdf_OutRealPlus( pBuff, pObj++, -Width )     || /* right-x */
              !pdf_OutRealPlus( pBuff, pObj++, -Width )     || /* top-y   */
              !pdf_OutStr( pBuff, " re s\r\n", 0 ) )
            return FALSE;
          break;
        }

      } else {
        /* !!! Tecky note.  The 1.3 spec. says if the BC array is empty, the border
        ** is transparent (which can only mean no border is drawn but space is
        ** allowed for one).  The spec says nothing about BC being absent
        ** altogether, but the same behaviour is applied here.
        */
      }

      *pBorderWidth = Width; /* Return border width even if none drawn */
    }
  }

  return TRUE;
}


/* ---------------------------------------------------------------------------
** pdf_DefPositioningRect()
**
**  This function defines a rectangle, within the annotation's basic bounding
**  box, which will be used for precise positioning of the text within the field.
**  In other words, if the annotation specifies a border is to be drawn, the
**  border itself occupies space within the bounding box, and if an inner margin
**  is required within that border, then the inner rectangle needs to be
**  smaller still.
*/
static Bool pdf_DefPositioningRect( PDF_IXC_PARAMS *ixc,
                                    USERVALUE BorderWidth,
                                    OBJECT *pBBox,       /* Input: Bounding box */
                                    OBJECT *pInnerRect ) /* Return: inner margin rect */
{
  OBJECT *pObj = oArray(*pBBox);      /* Point to 1st num of BBox */
  OBJECT *pRet = oArray(*pInnerRect);
  USERVALUE x, y, w, h ;

  /* Ensure the rect is converted to OREAL. */
  if ( !object_get_real(&pObj[0], &x) ||
       !object_get_real(&pObj[1], &y) ||
       !object_get_real(&pObj[2], &w) ||
       !object_get_real(&pObj[3], &h) )
    return FALSE ;

  object_store_real(&pRet[0], x) ;
  object_store_real(&pRet[1], y) ;
  object_store_real(&pRet[2], w) ;
  object_store_real(&pRet[3], h) ;

  if (BorderWidth > 0.0) {  /* Presence of a border restricts the field's rect. */
    oReal( pRet[0] ) += BorderWidth;                /* left-x origin   */
    oReal( pRet[1] ) += BorderWidth;                /* bottom-y origin */
    oReal( pRet[2] ) -= (BorderWidth * 2.0f);                /* width  */
    oReal( pRet[3] ) -= (BorderWidth * 2.0f);                /* height */

    /* Does the user want us to do as Acrobat 5.0 does? which is to further
    ** narrow the inner margin by the width of the border again!
    */
    if (ixc->AnnotParams.AcroFormParams.TextFieldInnerMarginAsBorder) {
      oReal( pRet[0] ) += BorderWidth;              /* left-x origin   */
      oReal( pRet[1] ) += BorderWidth;              /* bottom-y origin */
      oReal( pRet[2] ) -= (BorderWidth * 2.0f);              /* width  */
      oReal( pRet[3] ) -= (BorderWidth * 2.0f);              /* height */
    }
    else if (ixc->AnnotParams.AcroFormParams.TextFieldMinInnerMargin > 0.0) {
      /* Ensure the minimum margin is applied */
      USERVALUE Min = ixc->AnnotParams.AcroFormParams.TextFieldMinInnerMargin;
      oReal( pRet[0] ) += Min;                      /* left-x origin   */
      oReal( pRet[1] ) += Min;                      /* bottom-y origin */
      oReal( pRet[2] ) -= (Min * 2.0f);                      /* width  */
      oReal( pRet[3] ) -= (Min * 2.0f);                      /* height */
    }
  }
  else if (ixc->AnnotParams.AcroFormParams.TextFieldMinInnerMargin > 0.0) {
   /* Ensure the minimum margin is applied */
    USERVALUE Min = ixc->AnnotParams.AcroFormParams.TextFieldMinInnerMargin;
    oReal( pRet[0] ) += Min;                        /* left-x origin   */
    oReal( pRet[1] ) += Min;                        /* bottom-y origin */
    oReal( pRet[2] ) -= (Min * 2.0f);                        /* width  */
    oReal( pRet[3] ) -= (Min * 2.0f);                        /* height */
  }

  return TRUE;
}

/* ---------------------------------------------------------------------------
** pdf_ConstructAppearance()
** This function has to construct an appearance stream string (and stream
** dictionary) as a Form XObject.  It is given the acroform field's values
** via 'pFieldValues' and can assume that the appearance characteristics
** dictionary 'pdf_appchardict' has already been "matched" along with the
** other dictionaries.  The rules for how to create an appearance stream
** are ambiguous and complex. See p.440 of PDF manual 1.3, 2nd ed.
** Tiresomely, Acrobat 5.0 doesn't behave according to the manual.
*/
static Bool pdf_ConstructAppearance( PDFCONTEXT *pdfc,
                                     PDF_FORMFIELD *pFieldValues,
                                     OBJECT *pApDict,
                                     PDF_APPSTREAM_BUFF *pBuff,
                                     OBJECT *pBBox,
                                     OBJECT *pInnerRect )
{
  OBJECT NameObj = OBJECT_NOTVM_NOTHING;
  int16  FieldType;
  OBJECT *pObj = NULL;
  OBJECT *pBG = NULL;
  USERVALUE BorderWidth = 0.0;
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );


  /* Create a dictionary to support the appearance stream. */
  if (!pdf_create_dictionary( pdfc, PDF_APPDICT_LEN, pApDict ))
    return FALSE;


  /* Step 1:
  ** Define the Form XObject's 'Resources' key as the field value's /DR key.
  ** This will be done for us in pdf_SubmitField() which has to do the same
  ** for appearance streams readily obtained out the PDF source file.
  */

  /* Step 2:
  ** Define the Bounding Box on the basis of the Rect for the field.
  ** Note that the presence of /Rect in the annotation dict was not
  ** optional.  The Rect gives bottom-left and top-right coordinates
  ** while the BBox wants bottom-left coord then width & height.
  */
  if (!pdfWidge_RectToBBox( pdfannot_widgetdict[Ewd_Rect].result, pBBox ))
    return FALSE;

  theTags( NameObj ) = ONAME;
  oName( NameObj ) = system_names + NAME_BBox;
  if (!pdf_fast_insert_hash( pdfc, pApDict, &NameObj, pBBox ))
    return FALSE;

  /*
  **  Combine the lengths of the field's "value" and "default appearance" strings
  **  so that the required size of the output buffer can be sensibly estimated.
  */
  pBuff->pdfc = pdfc;
  theTags(pBuff->Buffer) = ONULL; /* Require the output buffer to be initialised */
  pBuff->Size = 0;

  FieldType = theINameNumber( oName(*(pFieldValues->pFieldType)) );
  if (FieldType == NAME_Tx) {
    if (pFieldValues->pValue != NULL)
      pBuff->Size = theLen( *(pFieldValues->pValue) );
    else if (pFieldValues->pDefValue != NULL)
      pBuff->Size = theLen( *(pFieldValues->pDefValue) );
  }

  if (pFieldValues->pDefAppear != NULL)
    pBuff->Size += theLen( *(pFieldValues->pDefAppear) );


  /*
  ** Drawing the field with a background color.
  ** If there's an appearance characteristics (/MK) dictionary defined, and
  ** if it specifies a background color for the text field (/BG), then draw
  ** a rectangle the size of the bounding box and fill it with the color.
  */
  if ((pdfannot_widgetdict[Ewd_MK].result != NULL) &&   /* MK key */
      (pdf_appchardict[Ead_BG].result != NULL))  {      /* BG key */

    pBG = pdf_appchardict[Ead_BG].result;   /* the array */

    if (theLen(*pBG) > 0) {   /* Array is not empty (empty => no background color) */

      if (!pdf_SetBackgroundColor( pBuff, theLen(*pBG), oArray(*pBG) ))
        return FALSE;

      /* Draw rectangle with the dimensions of the bounding box
      ** and fill it.
      */
      pObj = oArray(*pBBox);  /* Point to 1st num of BBox */
      if (!pdf_OutNum( pBuff, pObj++ ) ||
          !pdf_OutNum( pBuff, pObj++ ) ||
          !pdf_OutNum( pBuff, pObj++ ) ||
          !pdf_OutNum( pBuff, pObj++ ) ||
          !pdf_OutStr( pBuff, " re f\r\n", 0 ) )
        return FALSE;
    }
  }

  /* !!! Tecky Note:-  Discrepancy with spec.
  ** p.404 says if there's neither a /BS or /Border entry in the Widget dictionary,
  ** then a default border 1 point width, solid line, should be drawn.  Acrobat 5.0
  ** doesn't do this unless there's an /MK dictionary defined!  Also, according
  ** to the spec. (p.400), we should be able to ignore the /Border entry (& just
  ** refer to /BS alone) for Widget annotations. So I will.
  */
  if (!pdf_SetBorder( pdfc, pBuff, pBBox, &BorderWidth, pBG ))
    return FALSE;

  /* write out "/Tx BMC q" (& LF) */
  if (!pdf_OutStr( pBuff, " /Tx BMC q\r\n", 0 ))
    return FALSE;

  /*
  ** Write out the clipping rect. so text doesn't overwrite the border.
  ** This doesn't need doing if there's no border since Form XObjects are
  ** clipped to their bounding box anyway.
  */
  if (BorderWidth > 0.0)  {
    pObj = oArray(*pBBox);  /* Point to 1st num of BBox */
    if (!pdf_OutRealPlus( pBuff, pObj++, +BorderWidth ) ||   /* left-x   */
        !pdf_OutRealPlus( pBuff, pObj++, +BorderWidth ) ||   /* bottom-y */
        !pdf_OutRealPlus( pBuff, pObj++, (BorderWidth * -2.0f)) || /* width  */
        !pdf_OutRealPlus( pBuff, pObj++, (BorderWidth * -2.0f)) || /* height */
        !pdf_OutStr( pBuff, " re W n\r\n", 0 ) )
      return FALSE;
  }

  /* write out "BT" */
  if (!pdf_OutStr( pBuff, " BT ", 0 ))
    return FALSE;


  /* Write out /DA (default appearance) string which should just contain valid
  ** marking operators to be used directly.
  */
  if (pFieldValues->pDefAppear != NULL) {
    /* Output the appearance text directly. */
    if (!pdf_OutStr( pBuff, (char*) oString(*(pFieldValues->pDefAppear)), theLen(*pFieldValues->pDefAppear) ))
      return FALSE;
  }
  else {   /* Default Appearance string is null */
    /*
    ** The PDF spec (1.3, 2nd ed) says the /DA (& /DR) keys are mandatory
    ** for variable text fields.
    */
    HQFAIL( "No default appearance for variable text field in Acroform" );
    return error_handler( UNDEFINED );
  }

  if (FieldType == NAME_Tx) {
    /*
    ** Output the field's value text surrounded by brackets and followed by the Tj operator
    ** If there is no text, don't execute Tj.  Also, if the field is flagged as a password,
    ** then inhibit its display if the parameter so dictates.
    */
    if ((pFieldValues->pValue != NULL) || (pFieldValues->pDefValue != NULL)) {

      if (((pFieldValues->FieldFlags & ACROFORM_FLAG_PASSWORD) == 0) || /* not a password */
          (ixc->AnnotParams.AcroFormParams.DisplayPasswordValue)) {

        if (!pdf_OutStr( pBuff, " (", 0 ))
          return FALSE;

        if (pFieldValues->pValue != NULL) {
          if (!pdf_OutStr( pBuff, (char *) oString(*(pFieldValues->pValue)), theLen(*pFieldValues->pValue) ))
            return FALSE;
        }
        else if (pFieldValues->pDefValue != NULL) {
          if (!pdf_OutStr( pBuff, (char *) oString(*(pFieldValues->pDefValue)), theLen(*pFieldValues->pDefValue) ))
            return FALSE;
        }

        if (!pdf_OutStr( pBuff, ") Tj", 0 ))
          return FALSE;
      }
    }

    /* Calc rectangle to tightly bound the text within the field. */
    if (!pdf_DefPositioningRect( ixc, BorderWidth, pBBox, pInnerRect ))
      return FALSE;
  }

  /* Write out operators to conclude the appearance stream. */
  if (!pdf_OutStr( pBuff, " ET Q EMC\r\n", 0 ))
    return FALSE;

  return TRUE;
}

/* ----------------------------------------------------------------------------
** Takes the appearance string we've constructed locally and makes a proper(?!?)
** input stream out of it.
*/
static Bool pdf_StringToStream( PDFCONTEXT *pdfc,
                                OBJECT *pStrObj,
                                FILELIST *flptr,
                                OBJECT *pApStrm,
                                OBJECT *pApDict)
{
  PDFXCONTEXT *pdfxc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );


  /* Set up the FILELIST struct with the string as its "buffer"
   */
  string_decode_filter(flptr);
  theIFileLineNo(flptr) = 1;
  theIBuffer(flptr) = oString(*pStrObj);
  theIBufferSize(flptr) = theLen(*pStrObj);
  theIPtr(flptr)   = oString(*pStrObj);
  theICount(flptr) = theLen(*pStrObj);

  theLen( *pApStrm )  = theIFilterId(flptr);


  /* Initialise the filter */
  if (!(*theIMyInitFile(flptr))(flptr, pStrObj, &operandstack) )
    return (*theIFileLastError(flptr))(flptr);

  SetIRewindableFlag(flptr) ;
  SetIOpenFlag(flptr) ;    /* Now mark as open. */
  theIPDFContextID(flptr) = pdfxc->id;

  /* Create a Length object in the dictionary. */
  oName(nnewobj) = &system_names[ NAME_Length ];
  oInteger(inewobj) = theLen(*pStrObj);
  if ( !pdf_fast_insert_hash( pdfc, pApDict, &nnewobj, &inewobj) )
    return FALSE ;

  /** \todo @@@ TODO FIXME ajcd 2005-02-17: the string filter doesn't use
      this. What is it here for? */
  Copy(&theIParamDict(flptr), pApDict);

  file_store_object(pApStrm, flptr, EXECUTABLE) ;

  return TRUE;
}


/* ----------------------------------------------------------------------------
** pdf_SetMCparams()
** This function is a call-back, invoked from pdf_begin_marking_context(),
** specifically for the purposes of allowing us to initialise a new marking
** context (imc) with parameter values specific to AcroForm fields.  The new
** marking context is created for the annotation's "Form XObject" content
** stream - see pdf_SubmitField() which calls gs_execform().  The call-back
** mechanism allows us to specify arguments which, in this case, is via
** the PDF_CB_ARGS struct.  This pdf_SetMCparams() function is specified as
** the call-back via the call to pdf_set_mc_callback() later on.
*/
typedef struct {
  OBJECT        *pFieldRect;    /* Field rectangle array */
  PDF_FORMFIELD *pFieldValues;  /* Attributes & values of the field */
} PDF_CB_ARGS;

static Bool pdf_SetMCparams( PDF_IMC_PARAMS *new_imc, void *pArgs )
{
  PDF_CB_ARGS *pImcArgs = (PDF_CB_ARGS*) pArgs;

  new_imc->pFieldRect   = pImcArgs->pFieldRect;
  new_imc->pFieldValues = pImcArgs->pFieldValues;

  return TRUE;
}


/* -----------------------------------------------------------------------------
** Given a 'widget' annotation (which is a 'field' on an Acroform), executes the
** appearance stream as a Form XObject.
** Starting with the widget annotation / acroform field dictionary (given here
** as 'pDictObj'), the /AP key yields the appearance dictionary. The
** appearance dictionary contains an /N key which in turn yields an appearance
** stream which itself has a dictionary. The latter constitute a Form XObject.
*/

Bool pdf_Widget( PDFCONTEXT *pdfc, OBJECT *pDictObj )
{
  OBJECT *pApDict = NULL;
  OBJECT *pApStrm = NULL;
  OBJECT ApDict = OBJECT_NOTVM_NOTHING;
  OBJECT ApStrm = OBJECT_NOTVM_NOTHING;
  OBJECT *pApCharDict = NULL;
  int16  FieldType;
  PDF_FORMFIELD FieldValues = { 0 };  /* Initialise to null; ok for most attributes */
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  /*
  ** The following entites are defined here (though not explicitly used in this
  ** routine) so that they have enough permanance to last across the call to
  ** pdf_SubmitField() whereupon the function pdf_TextFieldAdjust() may be
  ** invoked as the appearance stream is being executed.  So, don't move 'em!
  */
  OBJECT BBox = OBJECT_NOTVM_NOTHING, InnerRect = OBJECT_NOTVM_NOTHING;
  OBJECT BBoxArr[4] = {
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
  } ;
  OBJECT RectArr[4] = {
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
  } ;
  FILELIST fl;
  PDF_APPSTREAM_BUFF Buff = {0};
  PDF_CB_ARGS Args;

  Buff.Buffer = onull ; /* set slot properties */

  theTags(BBox) = OARRAY | UNLIMITED | LITERAL;
  theLen(BBox) = 4;
  oArray(BBox) = BBoxArr;

  theTags(InnerRect) = OARRAY | UNLIMITED | LITERAL;
  theLen(InnerRect) = 4;
  oArray(InnerRect) = RectArr;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );


  /* Read the annotation dictionary. */
  if (!pdf_dictmatch( pdfc, pDictObj, pdfannot_widgetdict ))
    return FALSE;

  /* If there's an appearance characteristics dictionary to be had (from the
  ** /MK key) then go get it.  This is done here because some of the other
  ** (local) routines in this file assume 'pdf_appchardict' to be set-up (but
  ** not without first checking that the /MK key was present!).
  */
  pApCharDict = pdfannot_widgetdict[Ewd_MK].result;    /* /MK key present? */
  if (pApCharDict != NULL) {
    if (!pdf_dictmatch( pdfc, pApCharDict, pdf_appchardict ))
      return FALSE;
  }

  /* Obtain the appearance dictionary given by the /AP key.
  */
  pApDict = pdfannot_widgetdict[Ewd_AP].result;    /* /AP key */
  if (pApDict != NULL) {
    /* All we care about is the /N entry ("normal" appearance).  The others
    ** are for interactive use only (i.e. mouse-rollover and mouse-down).
    */
    if (!pdf_dictmatch( pdfc, pApDict, pdfappear_dict ))
      return FALSE;

    pApStrm = pdfappear_dict[pdfappear_N].result;   /* /N key */
    if (pApStrm == NULL)
      return error_handler( UNDEFINED );

    /* The /N appearance stream should be a stream.
    ** However, it can also be defined as a sub-dictionary in which case
    ** it lists a number of different "states" each with their own appearance.
    ** The actual state is given by the /AS entry in the annotation dictionary.
    */
    if (oType(*pApStrm) == ODICTIONARY) {
      OBJECT *pASobj = pdfannot_widgetdict[Ewd_AS].result;
      if (pASobj == NULL  ||  oType(*pASobj) != ONAME) {
        if (pASobj != NULL || ixc->strictpdf) {
          HQFAIL( "/AS state key invalid given /N dictionary" );
          return error_handler( TYPECHECK );
        }else{
          return TRUE; /* ignore widget is stream is missing */
        }
      }else{
        if (!pdf_ResolveAppStrm( pdfc, &pApStrm, pASobj ))
            return FALSE;
      }
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

      HQASSERT( oType(*pApDict) == ODICTIONARY, "appearance stream must have an ODICTIONARY." );
    }
  }

  /*
  **  Ascend the hierarchy of field dictionaries to obtain all the inheritable
  **  values available.
  */
  FieldValues.FieldFlags = ACROFORM_ATTR_UNDEFINED;
  FieldValues.MaxLen     = ACROFORM_ATTR_UNDEFINED;
  FieldValues.Quadding   = ACROFORM_ATTR_UNDEFINED;
  if (!pdf_AscendFields( pdfc, &FieldValues, pDictObj ))
    return FALSE;


  /*
  ** Thus far, an existing appearance stream may have been obtained.  Regardless,
  ** if the /NeedAppearances flag in the acroform dictionary is set to true,
  ** the appearance stream must be reconstructed, but only for Text fields.
  */
  FieldType = theINameNumber( oName(*(FieldValues.pFieldType)) );
  if (ixc->AcroForm.NeedAppearances  &&  FieldType == NAME_Tx) {
    /* For variable text streams /DA and /DR are mandatory. This
    ** is checked now!
    */
    if ((FieldValues.pDefAppear != NULL) && (FieldValues.pDefRsrcs  != NULL)) {
      /*
      ** This call to 'pdf_ConstructAppearance()' results in the construction
      ** of a string (in 'Buff') containing PDF marking operators (i.e. a contents
      ** stream), and a parameters dictionary in 'ApDict'
      */
      if (!pdf_ConstructAppearance( pdfc, &FieldValues, &ApDict, &Buff,
                                    &BBox, &InnerRect ))
        return FALSE;

      /*
      ** The string needs to be turned into a proper (input) "stream".
      */
      if (!pdf_StringToStream( pdfc, &Buff.Buffer, &fl, &ApStrm, &ApDict ))
        return FALSE;

      pApDict = &ApDict;
      pApStrm = &ApStrm;

      /*
      ** Note that the /DA string is likely to contain a Tf operator.
      ** With the usual content streams, there is no default font size for 'Tf'.
      ** But with AcroForm appearance streams, the size may be given as zero
      ** which means "...that the font is to be *auto-sized*: its size is computed
      ** as a function of the height of the annotation rectangle." (p.441, PDF
      ** manual 1.3, 2nd ed.).  F*%^&$ing 'eck!  Since the font isn't actually
      ** selected until Tj does its work, the scaling calculations can't be
      ** performed until the content stream (constructed by pdf_ConstructAppearance())
      ** is actually executed. For this reason (and others), the field parameters
      ** and bounding rectangle have to be passed through to the new marking
      ** context.  This is done via the 'pdf_set_mc_callback()' mechanism
      ** which reults in the local function 'pdf_SetMCparams()' being invoked
      ** to set the specific '*imc' parameters.
      */
      Args.pFieldValues = &FieldValues;
      Args.pFieldRect = &InnerRect;

      pdf_set_mc_callback( pdfxc, pdf_SetMCparams, &Args );
    }
    else {
      pApDict = NULL;
      pApStrm = NULL;
      /** \todo TODO(ADH).
      ** The absence of /DA and/or /DR keys in the field dictionary
      ** prevents the construction of an appearance stream.  It is not clear,
      ** however, whether or not we should be whinging about it.
      ** For now, we'll just let it slip past quietly.
      */
    }
  }


  /*
  ** By this point, we should have an appearance stream (either already present
  ** in the PDF or else re-constructed above).  It's now time to execute it.
  */
  if ((pApDict != NULL) && (pApStrm != NULL)) {
    if (!pdf_SubmitField( pdfc, pApDict, pApStrm, FieldValues.pDefRsrcs )) {
      pdf_set_mc_callback( pdfxc, NULL, NULL ); /* Reset in case of error */
      return FALSE;
    }
  }


  pdf_set_mc_callback( pdfxc, NULL, NULL ); /* Reset call-back address */

  /* Clean up */
  if ((oType(Buff.Buffer) == OSTRING) && (oString(Buff.Buffer) != NULL)) {
    theLen(Buff.Buffer) = CAST_TO_UINT16( Buff.Size );
    pdf_destroy_string( pdfc, Buff.Size, &Buff.Buffer );
  }

  if (pApDict == &ApDict)
    pdf_destroy_dictionary( pdfc, PDF_APPDICT_LEN, pApDict );

  return TRUE;
}

/* Log stripped */
