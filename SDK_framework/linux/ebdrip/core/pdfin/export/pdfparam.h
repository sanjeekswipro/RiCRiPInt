/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!export:pdfparam.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF parameter definitions
 */

#ifndef __PDFPARAM_H__
#define __PDFPARAM_H__

/** The following parameters apply to widget annotations
    (i.e. acroforms) only */
typedef struct pdf_acroform_params {
  /** FALSE => don't display text field's value if flagged as a
     password */
  Bool DisplayPasswordValue;
  /** Max. scale in multi-line text field */
  USERVALUE MultiLineMaxAutoFontScale;
  /** Min. scale in multi-line text field */
  USERVALUE MultiLineMinAutoFontScale;
  /** %age by which to decrement scale */
  USERVALUE MultiLineAutoFontScaleIncr;
  /** A minimum margin inside the field */
  USERVALUE TextFieldMinInnerMargin;
  /** Apply margin same width as border */
  Bool TextFieldInnerMarginAsBorder;
  /** For vertical positioning of the text */
  USERVALUE TextFieldDefVertDisplacement;
  /** For multi-line fields - the TL parameter */
  USERVALUE TextFieldFontLeading;
} PDF_ACROFORM_PARAMS;

/** Following parameters apply to all PDF annotations */
typedef struct pdf_annot_params {
  /** Do print acroform fields */
  Bool DoAcroForms;
  PDF_ACROFORM_PARAMS AcroFormParams;
  /** Override annotation's never "print" flag */
  Bool AlwaysPrint;
  /** Spec. says default is '1'; Acrobat applies default of '0' */
  int32  DefaultBorderWidth;
} PDF_ANNOT_PARAMS;

typedef struct pdf_optionalcontent_params {
  uint32 flags;
  OBJECT Intent;
  OBJECT Config;
  NAMECACHE * Event;

  OBJECT ON;
  OBJECT OFF;
  OBJECT Usage;
} PDF_OC_PARAMS;

/** OC flag bits */
#define OC_INTENT 1
#define OC_CONFIG 2
#define OC_EVENT  4

/** The following parameters apply to PDFin as a whole.

   N.B: consider if any additional parameters added here
   should also be reflected in the input execution context
   (pdf_ixc_params). */
typedef struct pdfparams {
  Bool   Strict ;
  OBJECT PageRange ;
  OBJECT OwnerPasswords ;
  OBJECT UserPasswords ;
  OBJECT PrivateKeyFiles ;
  OBJECT PrivateKeyPasswords ;
  Bool   HonorPrintPermissionFlag ;
  Bool   MissingFonts ;
  int32  PageCropTo ;
  int32  EnforcePDFVersion ;
  int32  AbortForInvalidTypes ;
  Bool   OverridePDFColorManagement ;
  Bool   PrintAnnotations ;
  PDF_ANNOT_PARAMS AnnotationParams ;
  Bool   ErrorOnFlateChecksumFailure ;
  Bool   IgnorePSXObjects ;
  Bool   WarnSkippedPages ;

  Bool   SizePageToBoundingBox ;
  PDF_OC_PARAMS OptionalContentOptions;
  Bool   EnableOptimizedPDFScan ;
  Bool   OptimizedPDFExternal ;
  int32  OptimizedPDFScanLimitPercent ;
  int32  OptimizedPDFCacheSize ;
  OBJECT OptimizedPDFCacheID ;
  OBJECT OptimizedPDFSetupID ;
  Bool   OptimizedPDFCrossXObjectBoundaries ;
  int32  OptimizedPDFSignificanceMask ;
  int32  OptimizedPDFScanWindow ;
  int32  OptimizedPDFImageThreshold ;
  Bool   ErrorOnPDFRepair ;
  Bool   PDFXVerifyExternalProfileCheckSums ;

  USERVALUE TextStrokeAdjust ;
  int32 XRefCacheLifetime ;

  /* PDFParams only: */
  Bool   PoorShowPage ;
  Bool   PoorSoftMask ;
  Bool   AdobeRenderingIntent ;
} PDFPARAMS ;



void initpdfparams(PDFPARAMS *params) ;
Bool pdf_set_oc_params( OBJECT *pdict, PDF_OC_PARAMS *options );
Bool pdf_set_annotation_params( OBJECT *pDict, PDF_ANNOT_PARAMS *pParams );

/* ============================================================================
Log stripped */

#endif /* __PDFPARAM_H__ */
