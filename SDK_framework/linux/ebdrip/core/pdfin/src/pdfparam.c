/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfparam.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF parameters; getting, setting, and default values.
 */

#include "core.h"
#include "mmcompat.h"   /* mm_alloc_static */
#include "swerrors.h"   /* STACKUNDERFLOW */
#include "objects.h"    /* OBOOLEAN */
#include "dictscan.h"   /* NAMETYPEMATCH */
#include "namedef_.h"   /* NAME_* */
#include "gschcms.h"    /* gsc_setPoorSoftMask */
#include "gstack.h"     /* gstateptr */

#include "dicthash.h"   /* insert_hash() */
#include "stacks.h"     /* operandstack */

#include "swpdfin.h"
#include "pdfdefs.h"
#include "pdfparam.h"


/* ---------------------------------------------------------------------- */

enum {
  pdf_match_Strict,
  pdf_match_PageRange,
  pdf_match_OwnerPasswords,
  pdf_match_UserPasswords,
  pdf_match_PrivateKeyFiles,
  pdf_match_PrivateKeyPasswords,
  pdf_match_HonorPrintPermissionFlag,
  pdf_match_MissingFonts,
  pdf_match_PageCropTo,
  pdf_match_EnforcePDFVersion,
  pdf_match_AbortForInvalidTypes,
  pdf_match_PrintAnnotations,
  pdf_match_AnnotationParams,
  pdf_match_ErrorOnFlateChecksumFailure,
  pdf_match_IgnorePSXObjects,
  pdf_match_WarnSkippedPages,
  pdf_match_SizePageToBoundingBox,
  pdf_match_OptionalContentOptions,
  pdf_match_EnableOptimizedPDFScan,
  pdf_match_OptimizedPDFScanLimitPercent,
  pdf_match_OptimizedPDFExternal,
  pdf_match_OptimizedPDFCacheSize,
  pdf_match_OptimizedPDFCacheID,
  pdf_match_OptimizedPDFSetupID,
  pdf_match_OptimizedPDFCrossXObjectBoundaries,
  pdf_match_OptimizedPDFSignificanceMask,
  pdf_match_OptimizedPDFScanWindow,
  pdf_match_OptimizedPDFImageThreshold,
  pdf_match_ErrorOnPDFRepair,
  pdf_match_PDFXVerifyExternalProfileCheckSums,
  pdf_match_TextStrokeAdjust,
  pdf_match_XRefCacheLifetime,
  pdf_match_PoorShowPage,
  pdf_match_PoorSoftMask,
  pdf_match_AdobeRenderingIntent,

  pdf_match_LENGTH
} ;

static NAMETYPEMATCH pdf_match[pdf_match_LENGTH + 1] = {
  { NAME_Strict | OOPTIONAL ,                       1, { OBOOLEAN }},
  { NAME_PageRange | OOPTIONAL ,                    2, { OARRAY, OPACKEDARRAY }},
  { NAME_OwnerPasswords | OOPTIONAL,                3, { OSTRING, OARRAY, OPACKEDARRAY }},
  { NAME_UserPasswords | OOPTIONAL,                 3, { OSTRING, OARRAY, OPACKEDARRAY }},
  { NAME_PrivateKeyFiles | OOPTIONAL,               3, { OSTRING, OARRAY, OPACKEDARRAY }},
  { NAME_PrivateKeyPasswords | OOPTIONAL,           3, { OSTRING, OARRAY, OPACKEDARRAY }},
  { NAME_HonorPrintPermissionFlag | OOPTIONAL ,     1, { OBOOLEAN }},
  { NAME_MissingFonts | OOPTIONAL ,                 1, { OBOOLEAN }},
  { NAME_PageCropTo | OOPTIONAL,                    1, { OINTEGER }},
  { NAME_EnforcePDFVersion | OOPTIONAL,             1, { OINTEGER }},
  { NAME_AbortForInvalidTypes | OOPTIONAL,          1, { OINTEGER }},
  { NAME_PrintAnnotations | OOPTIONAL,              1, { OBOOLEAN }},
  { NAME_AnnotationParams | OOPTIONAL,              1, { ODICTIONARY }},
  { NAME_ErrorOnFlateChecksumFailure | OOPTIONAL,   1, { OBOOLEAN }},
  { NAME_IgnorePSXObjects | OOPTIONAL,              1, { OBOOLEAN }},
  { NAME_WarnSkippedPages | OOPTIONAL,              1, { OBOOLEAN }},
  { NAME_SizePageToBoundingBox | OOPTIONAL,         1, { OBOOLEAN }},
  { NAME_OptionalContentOptions | OOPTIONAL,        1, { ODICTIONARY }},
  { NAME_EnableOptimizedPDFScan | OOPTIONAL,        1, { OBOOLEAN }},
  { NAME_OptimizedPDFScanLimitPercent | OOPTIONAL,  1, { OINTEGER }},
  { NAME_OptimizedPDFExternal | OOPTIONAL,          1, { OBOOLEAN }},
  { NAME_OptimizedPDFCacheSize | OOPTIONAL,         1, { OINTEGER }},
  { NAME_OptimizedPDFCacheID | OOPTIONAL,           1, { OSTRING }},
  { NAME_OptimizedPDFSetupID | OOPTIONAL,           1, { OSTRING }},
  { NAME_OptimizedPDFCrossXObjectBoundaries | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OptimizedPDFSignificanceMask | OOPTIONAL,  1,  { OINTEGER }},
  { NAME_OptimizedPDFScanWindow | OOPTIONAL,        1, { OINTEGER }},
  { NAME_OptimizedPDFImageThreshold | OOPTIONAL,    1, { OINTEGER }},
  { NAME_ErrorOnPDFRepair | OOPTIONAL,              1, { OBOOLEAN }},
  { NAME_PDFXVerifyExternalProfileCheckSums | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_TextStrokeAdjust | OOPTIONAL,              2, { OREAL, OINTEGER }},
  { NAME_XRefCacheLifetime | OOPTIONAL,             1, { OINTEGER }},
  { NAME_PoorShowPage | OOPTIONAL,                  1, { OBOOLEAN }},
  { NAME_PoorSoftMask | OOPTIONAL,                  1, { OBOOLEAN }},
  { NAME_AdobeRenderingIntent | OOPTIONAL,          1, { OBOOLEAN }},

  DUMMY_END_MATCH
};

/* Following is for annotations and, in particular, AcroForm 'widget' annotations */
enum {
  annots_DoAcroForms, annots_AcroFormParams, annots_AlwaysPrint,
  annots_DefaultBorderWidth, annots_dummy
} ;
static NAMETYPEMATCH pdf_annots_match[annots_dummy + 1] = {
  { NAME_DoAcroForms   | OOPTIONAL,   1, { OBOOLEAN }},    /* ie. do 'Widget' annotations */
  { NAME_AcroFormParams| OOPTIONAL,   1, { ODICTIONARY }}, /* 'Widget' annotations  */
  { NAME_AlwaysPrint   | OOPTIONAL,   1, { OBOOLEAN }},    /* override 'print' flag */
  { NAME_DefaultBorderWidth | OOPTIONAL, 1, { OINTEGER }}, /* override acrobat bug  */
  DUMMY_END_MATCH
};

/* Following is for 'widget' annotations - i.e. AcroForm fields */
enum {
  acroform_DisplayPasswordValue, acroform_MultiLineMaxAutoFontScale,
  acroform_MultiLineMinAutoFontScale, acroform_MultiLineAutoFontScaleIncr,
  acroform_TextFieldMinInnerMargin, acroform_TextFieldInnerMarginAsBorder,
  acroform_TextFieldDefVertDisplacement, acroform_TextFieldFontLeading,
  acroform_dummy
} ;
static NAMETYPEMATCH pdf_acroform_match[acroform_dummy + 1] = {
  { NAME_DisplayPasswordValue         | OOPTIONAL,  1, { OBOOLEAN }}, /* text fields */
  { NAME_MultiLineMaxAutoFontScale    | OOPTIONAL,  1, { OREAL }},
  { NAME_MultiLineMinAutoFontScale    | OOPTIONAL,  1, { OREAL }},
  { NAME_MultiLineAutoFontScaleIncr   | OOPTIONAL,  1, { OREAL }},
  { NAME_TextFieldMinInnerMargin      | OOPTIONAL,  1, { OREAL }},
  { NAME_TextFieldInnerMarginAsBorder | OOPTIONAL,  1, { OBOOLEAN }},
  { NAME_TextFieldDefVertDisplacement | OOPTIONAL,  1, { OREAL }},
  { NAME_TextFieldFontLeading         | OOPTIONAL,  1, { OREAL }},
  DUMMY_END_MATCH
};

/* ---------------------------------------------------------------------------

 Function:  pdf_set_acroform_params()
 Author:    Andre Hopper
 Created:   01 June 2001
 Arguments:
      (1) PostScript dictionary containing the parameter values.
      (2) Pointer to place where the values go.
 Description:
      Unpack parameters specific to PDF acroforms through the sub-dictionary
      of supplied values.
---------------------------------------------------------------------------- */
static Bool pdf_set_acroform_params( OBJECT *pDict, PDF_ACROFORM_PARAMS *pParams )
{
  OBJECT *pObj;

  if (!oCanRead(*oDict(*pDict)))
    if (!object_access_override(pDict))
      return error_handler( INVALIDACCESS );

  if (!dictmatch( pDict, pdf_acroform_match ))
    return FALSE;

  /* DisplayPasswordValue */
  if ((pObj = pdf_acroform_match[acroform_DisplayPasswordValue].result) != NULL)
    pParams->DisplayPasswordValue = oBool(*pObj);

  /* MultiLineMaxAutoFontScale */
  if ((pObj = pdf_acroform_match[acroform_MultiLineMaxAutoFontScale].result) != NULL)
    pParams->MultiLineMaxAutoFontScale = (USERVALUE) oReal(*pObj);

  /* MultiLineMinAutoFontScale */
  if ((pObj = pdf_acroform_match[acroform_MultiLineMinAutoFontScale].result) != NULL)
    pParams->MultiLineMinAutoFontScale = (USERVALUE) oReal(*pObj);

  /* MultiLineAutoFontScaleIncr */
  if ((pObj = pdf_acroform_match[acroform_MultiLineAutoFontScaleIncr].result) != NULL)
    pParams->MultiLineAutoFontScaleIncr = (USERVALUE) oReal(*pObj);

  /* TextFieldMinInnerMargin */
  if ((pObj = pdf_acroform_match[acroform_TextFieldMinInnerMargin].result) != NULL)
    pParams->TextFieldMinInnerMargin = (USERVALUE) oReal(*pObj);

  /* TextFieldInnerMarginAsBorder */
  if ((pObj = pdf_acroform_match[acroform_TextFieldInnerMarginAsBorder].result) != NULL)
    pParams->TextFieldInnerMarginAsBorder = oBool(*pObj);

  /* TextFieldDefVertDisplacement */
  if ((pObj = pdf_acroform_match[acroform_TextFieldDefVertDisplacement].result) != NULL)
    pParams->TextFieldDefVertDisplacement = oReal(*pObj);

  /* TextFieldFontLeading */
  if ((pObj = pdf_acroform_match[acroform_TextFieldFontLeading].result) != NULL)
    pParams->TextFieldFontLeading = oReal(*pObj);

  return TRUE;
}

/* ---------------------------------------------------------------------------

 Function:  pdf_set_annotation_params()
 Author:    Andre Hopper
 Created:   01 June 2001
 Arguments:
      (1) PostScript dictionary containing the parameter values.
      (2) Pointer to place where the values go.
 Description:
      Unpack parameters specific to PDF annotations through the sub-dictionary
      of supplied values.
---------------------------------------------------------------------------- */
Bool pdf_set_annotation_params( OBJECT *pDict, PDF_ANNOT_PARAMS *pParams )
{
  OBJECT *pObj;

  if (!oCanRead(*oDict(*pDict)))
    if (!object_access_override(pDict))
      return error_handler( INVALIDACCESS );

  if (!dictmatch( pDict, pdf_annots_match ))
    return FALSE;

  /* DoAcroForms */
  if ((pObj = pdf_annots_match[annots_DoAcroForms].result) != NULL)
    pParams->DoAcroForms = oBool(*pObj);

  /* AcroFormParams */
  if ((pObj = pdf_annots_match[annots_AcroFormParams].result) != NULL) {
    if (!pdf_set_acroform_params( pObj, &(pParams->AcroFormParams) ))
      return FALSE ;
  }

  /* AlwaysPrint */
  if ((pObj = pdf_annots_match[annots_AlwaysPrint].result) != NULL)
    pParams->AlwaysPrint = oBool(*pObj);

  /* DefaultBorderWidth */
  if ((pObj = pdf_annots_match[annots_DefaultBorderWidth].result) != NULL)
    pParams->DefaultBorderWidth = oInteger(*pObj);

  return TRUE;
}

/* ---------------------------------------------------------------------------

 Arguments:
      (1) Object through which to return a new PostScript dictionary containing
          the optional content (OC) options dictionary.
      (2) Pointer to current OC parameter values.
 Description:
      Defines a dictionary in which to return the current PDF optional content
      options dictionary.
---------------------------------------------------------------------------- */
#if 0
static Bool pdf_curr_oc_params( OBJECT *pDict, PDF_OC_PARAMS *pParams )
{
  /* create the sub-dictionary */
  if ( ! ps_dictionary(pDict, 3) )
    return FALSE;
#if 0 /* these are still to do - if required */
  oName(nnewobj) = &system_names[ NAME_Intent ];
  if (!insert_hash( pDict, &nnewobj,
                    pParams->Intent ? &tnewobj : &fnewobj ))
    return FALSE;
  oName(nnewobj) = &system_names[ NAME_Event ];
  if (!insert_hash( pDict, &nnewobj,
                    pParams->Event ? &tnewobj : &fnewobj ))
    return FALSE;
  oName(nnewobj) = &system_names[ NAME_Config ];
  if (!insert_hash( pDict, &nnewobj,
                    pParams->Config ? &tnewobj : &fnewobj ))
    return FALSE;
#endif

  return TRUE;
}
#endif

/* ---------------------------------------------------------------------------

 Function:  pdf_set_oc_params()
 Arguments:
      (1) PostScript dictionary containing the parameter values.
      (2) Pointer to place where the values go.
 Description:
      Unpack parameters specific to PDF OC options through the sub-dictionary
      of supplied values.
---------------------------------------------------------------------------- */
Bool pdf_set_oc_params( OBJECT *pdict, PDF_OC_PARAMS *options )
{
  enum { e_oc_intent, e_oc_config, e_oc_event,  e_oc_max};
  static NAMETYPEMATCH pdf_oc_match[e_oc_max+1] = {
    { NAME_Intent  | OOPTIONAL,   3, { OARRAY, OPACKEDARRAY, ONAME }},
    { NAME_Config  | OOPTIONAL,   1, { OSTRING }},
    { NAME_Event   | OOPTIONAL,   1, { ONAME }},
    DUMMY_END_MATCH
  };

  OBJECT *obj;
  NAMETYPEMATCH *pmatch= pdf_oc_match;

  if (!oCanRead(*oDict(*pdict)))
    if (!object_access_override(pdict))
      return error_handler( INVALIDACCESS );

  if (!dictmatch( pdict, pmatch ))
    return FALSE;

  if ((obj = pmatch[e_oc_intent].result) != NULL) {
    Copy(&options->Intent, obj) ;
    options->flags |= OC_INTENT;
  }

  if ((obj = pmatch[e_oc_config].result) != NULL) {
    Copy(&options->Config, obj);
    options->flags |= OC_CONFIG;
  }

  if ((obj = pmatch[e_oc_event].result) != NULL) {
    options->Event = oName(*obj);
    options->flags |= OC_EVENT;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------------

   function:            setpdfparams      author:              Eric Penfold
   creation date:       26-Sept-1997      last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool setpdfparams_(ps_context_t *pscontext)
{
  PDFPARAMS *pdfparams = ps_core_context(pscontext)->pdfparams;
  OBJECT *thed , *theo ;
  NAMETYPEMATCH *thematch = pdf_match ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  thed = theTop( operandstack ) ;

  if ( oType(*thed) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead(*oDict(*thed)) )
    if ( ! object_access_override(thed) )
      return error_handler( INVALIDACCESS ) ;

  if (! dictmatch( thed , thematch ))
    return FALSE ;

  /* Strict */
  if ((theo = thematch[ pdf_match_Strict ].result) != NULL)
    pdfparams->Strict = oBool(*theo) ;

  /* PageRange - Empty array prints all pages! */
  if ((theo = thematch[ pdf_match_PageRange ].result) != NULL)
    Copy( &pdfparams->PageRange, theo ) ;

  /* OwnerPasswords - Empty array clears passwords! */
  if ((theo = thematch[ pdf_match_OwnerPasswords ].result) != NULL)
    Copy( &pdfparams->OwnerPasswords, theo ) ;

  /* PrivateKeyFiles - Empty array clears list! */
  if ((theo = thematch[ pdf_match_PrivateKeyFiles ].result) != NULL)
    Copy( &pdfparams->PrivateKeyFiles, theo ) ;

  /* PrivateKeyPasswords - Empty array clears list! */
  if ((theo = thematch[ pdf_match_PrivateKeyPasswords ].result) != NULL)
    Copy( &pdfparams->PrivateKeyPasswords, theo ) ;

  /* UserPasswords - Empty array clears passwords! */
  if ((theo = thematch[ pdf_match_UserPasswords ].result) != NULL)
    Copy( &pdfparams->UserPasswords, theo ) ;

  /* HonorPrintPermissonFlag
   * Only writeable in non-RELEASE builds
   */
#if defined( DEBUG_BUILD )
  if ((theo = thematch[ pdf_match_HonorPrintPermissionFlag ].result) != NULL)
    pdfparams->HonorPrintPermissionFlag = oBool(*theo) ;
#endif

  /* MissingFonts */
  if ((theo = thematch[ pdf_match_MissingFonts ].result) != NULL)
    pdfparams->MissingFonts = oBool(*theo) ;

  /* PageCropTo */
  if ((theo = thematch[ pdf_match_PageCropTo ].result) != NULL) {
    if ( oInteger(*theo) < 0 ||
         oInteger(*theo) >= PDF_PAGECROPTO_N_VALUES )
      return error_handler( RANGECHECK ) ;
    pdfparams->PageCropTo = oInteger(*theo) ;
  }

  /* EnforcePDFVersion */
  if ((theo = thematch[ pdf_match_EnforcePDFVersion ].result) != NULL) {
    if ( oInteger(*theo) < 0 ||
         oInteger(*theo) >= PDF_ACCEPT_NUM_VALUES )
      return error_handler( RANGECHECK ) ;
    pdfparams->EnforcePDFVersion = oInteger(*theo) ;
  }

  /* AbortForInvalidTypes */
  if ((theo = thematch[ pdf_match_AbortForInvalidTypes ].result) != NULL) {
    if ( oInteger(*theo) < 0 ||
         oInteger(*theo) >= PDF_INVALIDTYPES_N_VALUES )
      return error_handler( RANGECHECK ) ;
    pdfparams->AbortForInvalidTypes = oInteger(*theo) ;
  }

  /* PrintAnnotations */
  if ((theo = thematch[ pdf_match_PrintAnnotations ].result) != NULL)
    pdfparams->PrintAnnotations = oBool(*theo) ;

  /* AnnotationParams */
  if ((theo = thematch[ pdf_match_AnnotationParams ].result) != NULL) {
    if (!pdf_set_annotation_params( theo, &(pdfparams->AnnotationParams) ))
      return FALSE ;
  }

  if ((theo = thematch[ pdf_match_ErrorOnFlateChecksumFailure ].result) != NULL) {
    pdfparams->ErrorOnFlateChecksumFailure = oBool( *theo ) ;
  }

  if ((theo = thematch[ pdf_match_IgnorePSXObjects ].result) != NULL) {
    pdfparams->IgnorePSXObjects = oBool( *theo ) ;
  }

  if ((theo = thematch[ pdf_match_WarnSkippedPages ].result) != NULL) {
    pdfparams->WarnSkippedPages = oBool( *theo ) ;
  }

  if ((theo = thematch[ pdf_match_SizePageToBoundingBox ].result) != NULL) {
    pdfparams->SizePageToBoundingBox = oBool( *theo ) ;
  }

  /* OptionalContentOptions */
  if ((theo = thematch[ pdf_match_OptionalContentOptions ].result) != NULL) {
    if (!pdf_set_oc_params( theo, &(pdfparams->OptionalContentOptions) ))
      return FALSE ;
  }

  /* EnableOptimizedPDFScan */
  if ((theo = thematch[ pdf_match_EnableOptimizedPDFScan ].result) != NULL) {
    pdfparams->EnableOptimizedPDFScan = oBool( *theo ) ;
  }

  /* OptimizedPDFScanLimitPercent */
  if ((theo = thematch[ pdf_match_OptimizedPDFScanLimitPercent ].result) != NULL) {
    pdfparams->OptimizedPDFScanLimitPercent = oInteger( *theo ) ;
  }

  /* OptimizedPDFExternal */
  if ((theo = thematch[pdf_match_OptimizedPDFExternal].result) != NULL) {
    pdfparams->OptimizedPDFExternal = oBool(*theo) ;
  }

  /* OptimizedPDFCacheSize */
  if ((theo = thematch[ pdf_match_OptimizedPDFCacheSize ].result) != NULL) {
    pdfparams->OptimizedPDFCacheSize = oInteger( *theo ) ;
  }

  /* OptimizedPDFCacheID */
  if ((theo = thematch[ pdf_match_OptimizedPDFCacheID ].result) != NULL) {
    Copy( &pdfparams->OptimizedPDFCacheID, theo ) ;
  }

  /* OptimizedPDFSetupID */
  if ((theo = thematch[ pdf_match_OptimizedPDFSetupID ].result) != NULL) {
    Copy( &pdfparams->OptimizedPDFSetupID, theo ) ;
  }

  /* OptimizedPDFCrossXObjectBoundaries */
  if ((theo = thematch[pdf_match_OptimizedPDFCrossXObjectBoundaries].result) != NULL) {
    pdfparams->OptimizedPDFCrossXObjectBoundaries = oBool(*theo) ;
  }

  /* OptimizedPDFSignificanceMask */
  if ((theo = thematch[pdf_match_OptimizedPDFSignificanceMask].result) != NULL) {
    pdfparams->OptimizedPDFSignificanceMask = oInteger(*theo) ;
  }

  /* OptimizedPDFScanWindow */
  if ((theo = thematch[pdf_match_OptimizedPDFScanWindow].result) != NULL) {
    pdfparams->OptimizedPDFScanWindow = oInteger(*theo) ;
  }

  /* OptimizedPDFImageThreshold */
  if ((theo = thematch[pdf_match_OptimizedPDFImageThreshold].result) != NULL) {
    pdfparams->OptimizedPDFImageThreshold = oInteger(*theo) ;
  }

  /* ErrorOnPDFRepair */
  if ((theo = thematch[pdf_match_ErrorOnPDFRepair].result) != NULL)
    pdfparams->ErrorOnPDFRepair = oBool(*theo);

  /* PDFXVerifyExternalProfileCheckSums */
  if ((theo = thematch[pdf_match_PDFXVerifyExternalProfileCheckSums].result) != NULL)
    pdfparams->PDFXVerifyExternalProfileCheckSums = oBool(*theo);

  /* TextStrokeAdjust */
  if ((theo = thematch[ pdf_match_TextStrokeAdjust ].result) != NULL) {
    pdfparams->TextStrokeAdjust = (oType(*theo) == OREAL) ? oReal(*theo)
                               : (USERVALUE) oInteger(*theo) ;
  }

  /* XRefCacheLifetime */
  if ((theo = thematch[pdf_match_XRefCacheLifetime].result) != NULL) {
    pdfparams->XRefCacheLifetime = oInteger(*theo) ;
  }

  /* PoorShowPage */
  if ((theo = thematch[ pdf_match_PoorShowPage ].result) != NULL) {
    pdfparams->PoorShowPage = oBool( *theo ) ;
  }

  /* PoorSoftMask */
  if ((theo = thematch[ pdf_match_PoorSoftMask ].result) != NULL) {
    pdfparams->PoorSoftMask = oBool( *theo ) ;

    /* Acrobat X always uses ICC alternate spaces within a soft mask group,
     * ignoring the profile. So we have to match that as default behaviour.
     */
    if (!gsc_setPoorSoftMask(gstateptr->colorInfo, pdfparams->PoorSoftMask))
      return FALSE;
  }

  /* AdobeRenderingIntent */
  if ((theo = thematch[ pdf_match_AdobeRenderingIntent ].result) != NULL) {
    pdfparams->AdobeRenderingIntent = oBool( *theo ) ;
  }

  pop( &operandstack ) ;
  return TRUE ;
}



/* ---------------------------------------------------------------------------

 Function:  pdf_curr_acroform_params()
 Author:    Andre Hopper
 Created:   01 June 2001
 Arguments:
      (1) Object through which to return a new PostScript dictionary containing
          the AcroForm parameters.
      (2) Pointer to current AcroForm parameter values.
 Description:
      Define a dictionary in which to return the current PDF AcroForm
      parameters.
---------------------------------------------------------------------------- */
static Bool pdf_curr_acroform_params( OBJECT *pDict, PDF_ACROFORM_PARAMS *pParams )
{
  /* create the sub-dictionary */
  if ( ! ps_dictionary(pDict, NUM_ARRAY_ITEMS(pdf_acroform_match) - 1) )
    return FALSE;

  /* DisplayPasswordValue */
  oName(nnewobj) = &system_names[ NAME_DisplayPasswordValue ];
  if (!insert_hash( pDict, &nnewobj,
                    pParams->DisplayPasswordValue ? &tnewobj : &fnewobj ))
    return FALSE;

  /* MultiLineMaxAutoFontScale */
  oName(nnewobj) = &system_names[ NAME_MultiLineMaxAutoFontScale ];
  oReal(rnewobj) = pParams->MultiLineMaxAutoFontScale;
  if (!insert_hash( pDict, &nnewobj, &rnewobj ))
    return FALSE;

  /* MultiLineMinAutoFontScale */
  oName(nnewobj) = &system_names[ NAME_MultiLineMinAutoFontScale ];
  oReal(rnewobj) = pParams->MultiLineMinAutoFontScale;
  if (!insert_hash( pDict, &nnewobj, &rnewobj ))
    return FALSE;

  /* MultiLineAutoFontScaleIncr */
  oName(nnewobj) = &system_names[ NAME_MultiLineAutoFontScaleIncr ];
  oReal(rnewobj) = pParams->MultiLineAutoFontScaleIncr;
  if (!insert_hash( pDict, &nnewobj, &rnewobj ))
    return FALSE;

  /* TextFieldMinInnerMargin */
  oName(nnewobj) = &system_names[ NAME_TextFieldMinInnerMargin ];
  oReal(rnewobj) = pParams->TextFieldMinInnerMargin;
  if (!insert_hash( pDict, &nnewobj, &rnewobj ))
    return FALSE;

  /* TextFieldInnerMarginAsBorder */
  oName(nnewobj) = &system_names[ NAME_TextFieldInnerMarginAsBorder ];
  if (!insert_hash( pDict, &nnewobj,
                    pParams->TextFieldInnerMarginAsBorder ? &tnewobj : &fnewobj ))
    return FALSE;

  /* TextFieldDefVertDisplacement */
  oName(nnewobj) = &system_names[ NAME_TextFieldDefVertDisplacement ];
  oReal(rnewobj) = pParams->TextFieldDefVertDisplacement;
  if (!insert_hash( pDict, &nnewobj, &rnewobj ))
    return FALSE;

  /* TextFieldFontLeading */
  oName(nnewobj) = &system_names[ NAME_TextFieldFontLeading ];
  oReal(rnewobj) = pParams->TextFieldFontLeading;
  if (!insert_hash( pDict, &nnewobj, &rnewobj ))
    return FALSE;

  return TRUE;
}


/* ---------------------------------------------------------------------------

 Function:  pdf_curr_annotation_params()
 Author:    Andre Hopper
 Created:   01 June 2001
 Arguments:
      (1) Object through which to return a new PostScript dictionary containing
          the annotation parameters.
      (2) Pointer to current annotation parameter values.
 Description:
      Defines a dictionary in which to return the current PDF annotation
      parameters.
---------------------------------------------------------------------------- */
static Bool pdf_curr_annotation_params( OBJECT *pDict, PDF_ANNOT_PARAMS *pParams )
{
  /* create the sub-dictionary */
  if ( ! ps_dictionary(pDict, NUM_ARRAY_ITEMS(pdf_annots_match) - 1) )
    return FALSE;

  /* DoAcroForms */
  oName(nnewobj) = &system_names[ NAME_DoAcroForms ];
  if (!insert_hash( pDict, &nnewobj,
                    pParams->DoAcroForms ? &tnewobj : &fnewobj ))
    return FALSE;

  /* AlwaysPrint */
  oName(nnewobj) = &system_names[ NAME_AlwaysPrint ];
  if (!insert_hash( pDict, &nnewobj,
                    pParams->AlwaysPrint ? &tnewobj : &fnewobj ))
    return FALSE;

  /* DefaultBorderWidth */
  oName(nnewobj) = &system_names[ NAME_DefaultBorderWidth ];
  oInteger(inewobj) = pParams->DefaultBorderWidth;
  if (!insert_hash( pDict, &nnewobj, &inewobj))
    return FALSE;


  /* Define a sub-dictionary to return the acroform parameters. */
  {
    OBJECT DictObj = OBJECT_NOTVM_NOTHING;

    if (!pdf_curr_acroform_params( &DictObj, &(pParams->AcroFormParams) ))
      return FALSE;

    oName(nnewobj) = &system_names[ NAME_AcroFormParams ];
    if (!insert_hash( pDict, &nnewobj, &DictObj ))
      return FALSE;
  }

  return TRUE;
}


/* ----------------------------------------------------------------------------
   function:            currentpdfparams  author:              Eric Penfold
   creation date:       26-Sept-1997      last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool currentpdfparams_(ps_context_t *pscontext)
{
  PDFPARAMS *pdfparams = ps_core_context(pscontext)->pdfparams;
  OBJECT thed = OBJECT_NOTVM_NOTHING ;

  if ( ! ps_dictionary(&thed, NUM_ARRAY_ITEMS(pdf_match) - 1) )
    return FALSE ;

  /* Strict */
  oName(nnewobj) = &system_names[ NAME_Strict ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->Strict ? &tnewobj : &fnewobj))
    return FALSE;

  /* PageRange */
  oName(nnewobj) = &system_names[ NAME_PageRange ] ;
  if ( ! insert_hash( &thed , &nnewobj ,
                      &pdfparams->PageRange ))
    return FALSE ;

  /* OwnerPasswords */
  oName(nnewobj) = &system_names[ NAME_OwnerPasswords ] ;
  if ( ! insert_hash( &thed , &nnewobj ,
                      &pdfparams->OwnerPasswords ))
    return FALSE ;

  /* UserPasswords */
  oName(nnewobj) = &system_names[ NAME_UserPasswords ] ;
  if ( ! insert_hash( &thed , &nnewobj ,
                      &pdfparams->UserPasswords ))
    return FALSE ;

#if defined( DEBUG_BUILD )
  /* HonorPrintPermissionFlag */
  oName(nnewobj) = &system_names[ NAME_HonorPrintPermissionFlag ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->HonorPrintPermissionFlag ? &tnewobj : &fnewobj))
    return FALSE ;
#endif

  /* MissingFonts */
  oName(nnewobj) = &system_names[ NAME_MissingFonts ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->MissingFonts ? &tnewobj : &fnewobj))
    return FALSE ;

  /* PageCropTo */
  oName(nnewobj) = &system_names[ NAME_PageCropTo ] ;
  oInteger(inewobj) = pdfparams->PageCropTo ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;

  /* EnforcePDFVersion */
  oName(nnewobj) = &system_names[ NAME_EnforcePDFVersion ] ;
  oInteger(inewobj) = pdfparams->EnforcePDFVersion ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;

  /* AbortForInvalidTypes */
  oName(nnewobj) = &system_names[ NAME_AbortForInvalidTypes ] ;
  oInteger(inewobj) = pdfparams->AbortForInvalidTypes ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;

  /* PrintAnnotations */
  oName(nnewobj) = &system_names[ NAME_PrintAnnotations ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->PrintAnnotations ? &tnewobj : &fnewobj))
    return FALSE;

  /* Define a sub-dictionary to return the annotation parameters. */
  {
    OBJECT DictObj = OBJECT_NOTVM_NOTHING;
    if (!pdf_curr_annotation_params( &DictObj, &(pdfparams->AnnotationParams) ))
      return FALSE;

    oName(nnewobj) = &system_names[ NAME_AnnotationParams ];
    if (!insert_hash( &thed, &nnewobj, &DictObj ))
      return FALSE;
  }

  /* ErrorOnFlateChecksumFailure */
  oName( nnewobj ) = &system_names[ NAME_ErrorOnFlateChecksumFailure ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->ErrorOnFlateChecksumFailure ? &tnewobj : &fnewobj))
    return FALSE;

  /* IgnorePSXObjects */
  oName( nnewobj ) = &system_names[ NAME_IgnorePSXObjects ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->IgnorePSXObjects ? &tnewobj : &fnewobj))
    return FALSE;

  /* WarnSkippedPages */
  oName( nnewobj ) = &system_names[ NAME_WarnSkippedPages ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->WarnSkippedPages ? &tnewobj : &fnewobj))
    return FALSE;

  /* SizePageToBoundingBox */
  oName( nnewobj ) = &system_names[ NAME_SizePageToBoundingBox ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->SizePageToBoundingBox ? &tnewobj : &fnewobj))
    return FALSE;

#if 0 /* todo disabled for now until required */
  /* Define a sub-dictionary to return the OptionalContentOptions */
  {
    OBJECT dictobj = OBJECT_NOTVM_NOTHING;
    if (!pdf_curr_oc_params( &dictobj, &(pdfparams->OptionalContentOptions) ))
      return FALSE;

    oName(nnewobj) = &system_names[ NAME_OptionalContentOptions ];
    if (!insert_hash( &thed, &nnewobj, &dictobj ))
      return FALSE;
  }
#endif

  /* PrivateKeyFiles */
  oName(nnewobj) = &system_names[ NAME_PrivateKeyFiles ] ;
  if ( ! insert_hash( &thed , &nnewobj ,
                      &pdfparams->PrivateKeyFiles ))
    return FALSE ;

  /* UserPasswords */
  oName(nnewobj) = &system_names[ NAME_PrivateKeyPasswords ] ;
  if ( ! insert_hash( &thed , &nnewobj ,
                      &pdfparams->PrivateKeyPasswords ))
    return FALSE ;

  /* EnableOptimizedPDFScan */
  oName( nnewobj ) = &system_names[ NAME_EnableOptimizedPDFScan ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->EnableOptimizedPDFScan ? &tnewobj : &fnewobj))
    return FALSE;

  /* OptimizedPDFScanLimitPercent */
  oName( nnewobj ) = &system_names[ NAME_OptimizedPDFScanLimitPercent ] ;
  oInteger(inewobj) = pdfparams->OptimizedPDFScanLimitPercent ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;

  /* OptimizedPDFExternal */
  oName( nnewobj ) = &system_names[ NAME_OptimizedPDFExternal ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->OptimizedPDFExternal ? &tnewobj : &fnewobj))
    return FALSE ;

  /* OptimizedPDFCacheSize */
  oName( nnewobj ) = &system_names[ NAME_OptimizedPDFCacheSize ] ;
  oInteger(inewobj) = pdfparams->OptimizedPDFCacheSize ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;

  /* OptimizedPDFCacheID */
  oName(nnewobj) = &system_names[ NAME_OptimizedPDFCacheID ] ;
  if ( ! insert_hash( &thed , &nnewobj ,
                      &pdfparams->OptimizedPDFCacheID ))
    return FALSE ;

  /* OptimizedPDFSetupID */
  oName(nnewobj) = &system_names[ NAME_OptimizedPDFSetupID ] ;
  if ( ! insert_hash( &thed , &nnewobj ,
                      &pdfparams->OptimizedPDFSetupID ))
    return FALSE ;

#if 0 /* These two are undocumented so we'll not advertise their presence */
  /* OptimizedPDFCrossXObjectBoundaries */
  oName( nnewobj ) = &system_names[ NAME_OptimizedPDFCrossXObjectBoundaries ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->OptimizedPDFCrossXObjectBoundaries ? &tnewobj : &fnewobj))
    return FALSE ;

  /* OptimizedPDFSignificanceMask */
  oName( nnewobj ) = &system_names[ NAME_OptimizedPDFSignificanceMask ] ;
  oInteger(inewobj) = pdfparams->OptimizedPDFSignificanceMask ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;
#endif

  /* OptimizedPDFScanWindow */
  oName( nnewobj ) = &system_names[ NAME_OptimizedPDFScanWindow ] ;
  oInteger(inewobj) = pdfparams->OptimizedPDFScanWindow ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;

  /* OptimizedPDFImageThreshold */
  oName( nnewobj ) = &system_names[ NAME_OptimizedPDFImageThreshold ] ;
  oInteger(inewobj) = pdfparams->OptimizedPDFImageThreshold ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;

  /* ErrorOnPDFRepair */
  oName(nnewobj) = &system_names[NAME_ErrorOnPDFRepair];
  if ( !insert_hash(&thed, &nnewobj,
                    pdfparams->ErrorOnPDFRepair ? &tnewobj : &fnewobj))
    return FALSE;

  /* PDFXVerifyExternalProfileCheckSums */
  oName(nnewobj) = &system_names[NAME_PDFXVerifyExternalProfileCheckSums];
  if ( !insert_hash(&thed, &nnewobj,
                    pdfparams->PDFXVerifyExternalProfileCheckSums ?
                      &tnewobj : &fnewobj))
    return FALSE;

  { /* TextStrokeAdjust */
    OBJECT real = OBJECT_NOTVM_REAL(OBJECT_0_0F) ;
    oName( nnewobj ) = &system_names[ NAME_TextStrokeAdjust ] ;
    oReal(real) = pdfparams->TextStrokeAdjust ;
    if (! insert_hash( &thed, &nnewobj, &real))
      return FALSE ;
  }

  /* XRefCacheLifetime */
  oName( nnewobj ) = &system_names[ NAME_XRefCacheLifetime ] ;
  oInteger(inewobj) = pdfparams->XRefCacheLifetime ;
  if (! insert_hash( &thed, &nnewobj, &inewobj))
    return FALSE ;

  /* PoorShowPage */
  oName( nnewobj ) = &system_names[ NAME_PoorShowPage ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->PoorShowPage ? &tnewobj : &fnewobj))
    return FALSE;

  /* PoorSoftMask */
  oName( nnewobj ) = &system_names[ NAME_PoorSoftMask ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->PoorSoftMask ? &tnewobj : &fnewobj))
    return FALSE;

  /* AdobeRenderingIntent */
  oName( nnewobj ) = &system_names[ NAME_AdobeRenderingIntent ] ;
  if (! insert_hash( &thed, &nnewobj,
                     pdfparams->AdobeRenderingIntent ? &tnewobj : &fnewobj))
    return FALSE;

  /* Put the dictionary on the stack to return it. */
  return push( &thed , &operandstack ) ;
}

#define initEmptyArray( _array_obj ) MACRO_START          \
  (void)object_slot_notvm(&_array_obj) ;  \
  theTags( _array_obj ) = OARRAY | LITERAL | UNLIMITED ;  \
  theLen( _array_obj )  = 0 ;                             \
  oArray( _array_obj )  = NULL ; \
MACRO_END

#define initEmptyString( _string_obj ) MACRO_START          \
  (void)object_slot_notvm(&_string_obj) ;  \
  theTags( _string_obj ) = OSTRING | LITERAL | UNLIMITED ;       \
  theLen( _string_obj )  = 0 ;                             \
  oString( _string_obj )  = NULL ; \
MACRO_END

void initpdfparams(PDFPARAMS *params)
{
  PDF_ACROFORM_PARAMS *pAc;

  HQASSERT(pdf_match_LENGTH == NUM_ARRAY_ITEMS(pdf_match) - 1,
           "pdf_match enumeration out of step with NAMETYPEMATCH") ;

  params->Strict                    = FALSE ;
  params->MissingFonts              = FALSE ;
#if defined( DEBUG_BUILD )
  params->HonorPrintPermissionFlag  = TRUE ;
#endif

  initEmptyArray( params->PageRange ) ;
  initEmptyArray( params->OwnerPasswords ) ;
  initEmptyArray( params->UserPasswords ) ;
  params->PageCropTo = PDF_PAGECROPTO_MEDIABOX ;
  params->EnforcePDFVersion = PDF_ACCEPT_ANY;
  params->AbortForInvalidTypes = PDF_INVALIDTYPES_WARN ;

  params->PrintAnnotations = TRUE;
  params->AnnotationParams.DoAcroForms = TRUE;
  params->AnnotationParams.AlwaysPrint = FALSE;
  params->AnnotationParams.DefaultBorderWidth = 1;

  params->ErrorOnFlateChecksumFailure = TRUE ;
  params->IgnorePSXObjects = TRUE ;
  params->WarnSkippedPages = TRUE ;
  params->SizePageToBoundingBox = TRUE ;

  pAc = &params->AnnotationParams.AcroFormParams;
  pAc->DisplayPasswordValue = FALSE;
  pAc->MultiLineMaxAutoFontScale  = 12.0f;
  pAc->MultiLineMinAutoFontScale  = 2.0f;
  pAc->MultiLineAutoFontScaleIncr = 0.05f;
  pAc->TextFieldMinInnerMargin    = 2.0f;
  pAc->TextFieldInnerMarginAsBorder = TRUE;
  pAc->TextFieldDefVertDisplacement = 0.3f;
  pAc->TextFieldFontLeading = 0.08f;

  params->OptionalContentOptions.flags = 0;
  params->OptionalContentOptions.Intent = onothing ; /* set slot properties */
  params->OptionalContentOptions.Config = onothing ; /* set slot properties */
  params->OptionalContentOptions.ON = onothing ; /* set slot properties */
  params->OptionalContentOptions.OFF = onothing ; /* set slot properties */

  initEmptyArray( params->PrivateKeyFiles ) ;
  initEmptyArray( params->PrivateKeyPasswords ) ;

  params->EnableOptimizedPDFScan = FALSE ;
  params->OptimizedPDFScanLimitPercent = 10 ;
  params->OptimizedPDFExternal = FALSE ;
  params->OptimizedPDFCacheSize = 1024 ;
  initEmptyString( params->OptimizedPDFCacheID ) ;
  initEmptyString( params->OptimizedPDFSetupID ) ;
  params->OptimizedPDFCrossXObjectBoundaries = TRUE ;
  /* By default everything but char, rect, quad and fill is
     significant. */
  params->OptimizedPDFSignificanceMask = 0x3FF0 ;
  params->OptimizedPDFScanWindow = 0 ; /** \todo Change to something
                                           sensible when ready */
  params->OptimizedPDFImageThreshold = 32 ;
  params->ErrorOnPDFRepair = FALSE;
  params->PDFXVerifyExternalProfileCheckSums = FALSE;
  params->TextStrokeAdjust = 0.0 ;
  params->XRefCacheLifetime = 10 ;

  params->PoorShowPage = FALSE ;
  params->PoorSoftMask = DEFAULT_POOR_SOFT_MASK ;
  params->AdobeRenderingIntent = TRUE ;
}

/* ============================================================================
Log stripped */
