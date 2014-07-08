/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfacrof.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Contains routines for handling an AcroForm within a PDF document.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"

#include "swpdf.h"
#include "pdfmatch.h"
#include "pdfin.h"

#include "pdfacrof.h"


/* ------------------------------------------------------------------------- 
**  Read the AcroForm dictionary (from the Catalog dictionary) and save
**  the values we're interested in in the pdfin execution context.
*/
int32 pdf_get_AcroForm( PDFCONTEXT *pdfc, PDF_IXC_PARAMS *ixc, OBJECT *pDictObj )
{
  /*
  ** The AcroForm dictionary is obtained from the Catalog dictionary.
  ** It contains a list of all the fields defining the AcroForm for the
  ** whole document (which we don't need) but also some default values
  ** for the fields which we might need.
  */
  STATIC NAMETYPEMATCH pdf_acroform_dict[] = {
  /* 0 */  { NAME_Fields,          3, { OARRAY, OPACKEDARRAY, OINDIRECT }},   /* Required apparently */
  /* 1 */  { NAME_NeedAppearances | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
  /* .     { NAME_SigFlags | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},       */
  /* .     { NAME_CO | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }}, */
  /* 2 */  { NAME_DR | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
  /* 3 */  { NAME_DA | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
  /* 4 */  { NAME_Q  | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},
           DUMMY_END_MATCH
  };

  /* PDF spec says Fields entry is mandatory, but Adobe will accept PDF if it 
   * missing. So do the same unless strictpdf is enabled.
   */
  if (!ixc->strictpdf)
    pdf_acroform_dict[0].name |=  OOPTIONAL;
  else
    pdf_acroform_dict[0].name &= ~OOPTIONAL;

  if (!pdf_dictmatch( pdfc, pDictObj, pdf_acroform_dict ))
    return FALSE;

  if (pdf_acroform_dict[1].result != NULL)
    ixc->AcroForm.NeedAppearances = oBool( *(pdf_acroform_dict[1].result) ); 

  if (pdf_acroform_dict[2].result != NULL)
    ixc->AcroForm.pDefRsrcs = pdf_acroform_dict[2].result;

  if (pdf_acroform_dict[3].result != NULL)
    ixc->AcroForm.pDefAppear = pdf_acroform_dict[3].result;

  if (pdf_acroform_dict[4].result != NULL)
    ixc->AcroForm.Quadding = oInteger( *(pdf_acroform_dict[4].result) );

  return TRUE;
}


/* ------------------------------------------------------------------------- 
** pdf_ValidateValue()
** Validates the value of a /V or /DV key in an acroform field dictionary.
** Text Fields are documented as having string values. Choice fields can have 
** string, array of strings and null as values.  Signature fields have 
** dictionaries.  Buttons are documented as having Name values (for the state)
** but some have been given strings in the Adobe-provided example forms. Since
** we don't use /V(alue) for buttons, we do not check its type here.
*/
static int32 pdf_ValidateValue( int16 NameNum, OBJECT *pValObj )
{
  switch (NameNum) 
  {
  case NAME_Tx:
    if (oType(*pValObj) != OSTRING) {
      HQFAIL( "AcroForm Tx field is of invalid type" );
      return error_handler( TYPECHECK );
    }
    break;

  case NAME_Ch:
    if (oType(*pValObj) != OSTRING &&
        oType(*pValObj) != OARRAY  &&
        oType(*pValObj) != OPACKEDARRAY &&
        oType(*pValObj) != ONULL) {
      HQFAIL( "AcroForm Ch field is of invalid type" );
      return error_handler( TYPECHECK );
    }
    break;

  case NAME_Sig:
    if (oType(*pValObj) != ODICTIONARY) {
      HQFAIL( "AcroForm Sig field is of invalid type" );
      return error_handler( TYPECHECK );
    }
    break;

  case NAME_Btn:    /* Don't check because not used. */
    break;

  default:
    HQFAIL( "Unknown AcroForm field type in pdf_ValidateValue" );
    return error_handler(UNDEFINED);
  }

  return TRUE;
}

/* ------------------------------------------------------------------------- 
** pdf_AscendFields()
** The widget annotation (dictionary) shares the _terminal_ field dictionary.
** However, field dictionaries can be hierarchically organised (but not
** annotation dictionaries!!) and a number of the attributes inherited rather 
** than re-defined each time.  From the terminal field, we have to ascend the 
** hierarchy of field dictionaries until we have values for all the 
** attributes we're interested in.  Not all keys will be defined, however - 
** some have defaults.  And some are defined in the AcroForm dictionary (as 
** a last resort).
** 'pdf_AscendFields()', therefore, and the structure (PDF_FORMFIELD)
** that it fills, is independent of any annotation and is specific to the 
** hierarchy of fields.
*/

int32 pdf_AscendFields( PDFCONTEXT *pdfc, PDF_FORMFIELD *pValues, OBJECT *pDictObj )
{
  int16 NameNum;
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  /*
  ** The acroform _field_ dictionary.  This one contains (a) entries common to 
  ** all acroform fields (table 7.44); (b) entries specific to fields with 
  ** variable text (table 7.46), and (c) entries specific to text fields 
  ** (table 7.50).  [ Ref:- PDF manual 1.3, 2nd ed. ]
  ** The real need for this dictionary set is that the keys are all inheritable.
  ** The actual dictionary for a text field of an AcroForm is merged with that 
  ** of the (widget) annotation dictionary.
  ** Note also that all the field keys we are interested in are inheritable
  ** through the potential hierarchy of field dictionaries and therefore have
  ** to be marked here as optional even though ultimately they may be mandatory.
  */
  STATIC NAMETYPEMATCH pdf_fielddict[] = {
          /* Entries common to all Acroform fields */
  /* 0 */ { NAME_FT      | OOPTIONAL, 2, { ONAME, OINDIRECT }},       /* Field Type */
  /* 1 */ { NAME_Parent  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
  /* 2 */ { NAME_Kids    | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  /* .    { NAME_T       | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
  /* .    { NAME_TU      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
  /* .    { NAME_TM      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */
  /* 3 */ { NAME_Ff      | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},    /* field flags   */
  /* 4 */ { NAME_V       | OOPTIONAL, 7, { OSTRING, ONAME, ODICTIONARY, OARRAY, OPACKEDARRAY, ONULL, OINDIRECT }}, /* Value    */
  /* 5 */ { NAME_DV      | OOPTIONAL, 7, { OSTRING, ONAME, ODICTIONARY, OARRAY, OPACKEDARRAY, ONULL, OINDIRECT }}, /* Default value */
  /* .    { NAME_AA      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, */

          /* Entries specific to fields with "variable text" */
  /* 6 */ { NAME_DR      | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }}, /* Default resources  */
  /* 7 */ { NAME_DA      | OOPTIONAL, 2, { OSTRING, OINDIRECT }},     /* Default appearance */
  /* 8 */ { NAME_Q       | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},    /* Quadding           */

          /* Entries specific to text fields */
  /* 9 */ { NAME_MaxLen  | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},    /* Max num characters */
            DUMMY_END_MATCH
  };

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );


  /* Unravel the field dictionary */
  if (!pdf_dictmatch( pdfc, pDictObj, pdf_fielddict ))
    return FALSE;

  /* Extract values present if not already got a value */
  if (pdf_fielddict[0].result != NULL) {   /* field stype */
    if (pValues->pFieldType == NULL)
      pValues->pFieldType = pdf_fielddict[0].result;
  }

  if (pdf_fielddict[3].result != NULL) {   /* field flags */
    if (pValues->FieldFlags == ACROFORM_ATTR_UNDEFINED)
      pValues->FieldFlags = oInteger( *(pdf_fielddict[3].result) );
  }

  if (pdf_fielddict[4].result != NULL) {   /* value */
    if (pValues->pValue == NULL)
      pValues->pValue = pdf_fielddict[4].result;
  }

  if (pdf_fielddict[5].result != NULL) {  /* default value */
    if (pValues->pDefValue == NULL)
      pValues->pDefValue = pdf_fielddict[5].result;
  }

  if (pdf_fielddict[6].result != NULL) {  /* default resources dictionary */
    if (pValues->pDefRsrcs == NULL)
      pValues->pDefRsrcs = pdf_fielddict[6].result;
  }

  if (pdf_fielddict[7].result != NULL) {  /* default appearances string */
    if (pValues->pDefAppear == NULL)
      pValues->pDefAppear = pdf_fielddict[7].result;
  }

  if (pdf_fielddict[8].result != NULL) {  /* quadding */
    if (pValues->Quadding == ACROFORM_ATTR_UNDEFINED)
      pValues->Quadding = oInteger( *(pdf_fielddict[8].result) );
  }

  if (pdf_fielddict[9].result != NULL) {  /* Max num characters */
    if (pValues->MaxLen == ACROFORM_ATTR_UNDEFINED)
      pValues->MaxLen = oInteger( *(pdf_fielddict[9].result) );
  }


  /* If all values defined, can quit */
  if ((pValues->pFieldType != NULL) &&
      (pValues->FieldFlags != ACROFORM_ATTR_UNDEFINED) &&
      (pValues->pValue != NULL)     &&
      (pValues->pDefValue != NULL)  &&
      (pValues->pDefRsrcs != NULL)  &&
      (pValues->pDefAppear != NULL) &&
      (pValues->Quadding != ACROFORM_ATTR_UNDEFINED) &&
      (pValues->MaxLen != ACROFORM_ATTR_UNDEFINED))    {
    return TRUE;
  }

  /* If the field has a parent, ascend to it to get its values.
     Note, when the recursion reaches the top-most field, the
     subsequent checks are then done.
  */
  if (pdf_fielddict[1].result != NULL)
    return pdf_AscendFields( pdfc, pValues, pdf_fielddict[1].result );


  /* No parent, or no more parents(!).  Get remaining default values
  ** from the document's "AcroForm" - the values of which have been
  ** saved in the execution context.
  */
  if (pValues->pDefRsrcs == NULL)
    pValues->pDefRsrcs = ixc->AcroForm.pDefRsrcs;

  if (pValues->pDefAppear == NULL)
    pValues->pDefAppear = ixc->AcroForm.pDefAppear;

  if (pValues->Quadding == ACROFORM_ATTR_UNDEFINED)
    pValues->Quadding = ixc->AcroForm.Quadding;


  /* 
  ** Check non-optional keys are defined, and that undefined optional
  ** fields are given their default values.
  ** Note that /DR & /DA only required if there's variable text - this
  ** is asserted later on when they might be needed.
  */
  if (pValues->pFieldType == NULL)  /* No acroform field type defined. */
    return error_handler( UNDEFINED );

  /* Guard against being given empty strings for which the scanner
  ** leaves a null pointer in the string object.
  */
  if (pValues->pValue != NULL)
    if (oType(*(pValues->pValue)) == OSTRING)
      if (theILen(pValues->pValue) == 0)
        pValues->pValue = NULL;

  if (pValues->pDefValue != NULL)
    if (oType(*(pValues->pDefValue)) == OSTRING)
      if (theILen(pValues->pDefValue) == 0)
        pValues->pDefValue = NULL;


  /* Only accept known field types */
  NameNum = theINameNumber( oName(*(pValues->pFieldType)) );
  if ((NameNum != NAME_Btn) && (NameNum != NAME_Tx) &&
      (NameNum != NAME_Ch)  && (NameNum != NAME_Sig))   {
    return error_handler( UNDEFINED );
  }

  /* Ensure defined values are of the correct type for the specific types 
  ** of fields.
  */
  if (pValues->pValue != NULL)
    if (!pdf_ValidateValue( NameNum, pValues->pValue ))
      return FALSE;

  if (pValues->pDefValue != NULL)
    if (!pdf_ValidateValue( NameNum, pValues->pDefValue ))
      return FALSE;

  /* Apply defaults */
  if (pValues->FieldFlags == ACROFORM_ATTR_UNDEFINED)
    pValues->FieldFlags = 0;

  if (pValues->Quadding == ACROFORM_ATTR_UNDEFINED)
    pValues->Quadding = ACROFORM_QUAD_LEFT;  /* Default value: table 7.46, PDF 1.3, 2nd ed. */

  return TRUE;
}

/* ----------------------------------------------------------------------------
** Normalises the coords of a rectangle so that it is specified as bottom-left
** and top-right.  This is needed so that other calculations on the rectangle
** (e.g. width and height) are straight forward.
** The parameter is a pointer to the array 'object' which in turn points to
** a contiguous array of four objects representing the rectangle's coords.
*/
int32 pdf_NormaliseRect( OBJECT *pRect )
{
  USERVALUE x1, y1, x2, y2;
  OBJECT *pObj;
  
  HQASSERT( (pRect != NULL), "pRect null in pdf_NormaliseRect" );
  HQASSERT( (oType(*pRect)==OARRAY), "*pRect not an array in pdf_NormaliseRect" );

  /* Obtain current values */
  pObj = oArray(*pRect);
  x1 = (USERVALUE) object_numeric_value( &pObj[0] );
  y1 = (USERVALUE) object_numeric_value( &pObj[1] );
  x2 = (USERVALUE) object_numeric_value( &pObj[2] );
  y2 = (USERVALUE) object_numeric_value( &pObj[3] );

  /* Write back normalised values */
  oReal( pObj[0] ) = min( x1, x2 );
  oReal( pObj[1] ) = min( y1, y2 );
  oReal( pObj[2] ) = max( x1, x2 );
  oReal( pObj[3] ) = max( y1, y2 );

  /* All values are now reals */
  theTags( pObj[0] ) = OREAL | LITERAL | UNLIMITED;
  theTags( pObj[1] ) = OREAL | LITERAL | UNLIMITED;
  theTags( pObj[2] ) = OREAL | LITERAL | UNLIMITED;
  theTags( pObj[3] ) = OREAL | LITERAL | UNLIMITED;

  return TRUE;
}



/* Log stripped */
