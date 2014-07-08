/**
 * \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfxChecks.c(EBDSDK_P.1) $
 * $Id: src:pdfxChecks.c,v 1.7.1.1.1.1 2013/12/19 11:25:14 anon Exp $
 *
 * Copyright (C) 2004-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF/X check and 'detected' function definitions.
 */

#include "core.h"
#include "pdfx.h"
#include "pdfxPrivate.h"

#include "pdfin.h"
#include "pdfmatch.h"
#include "pdfcolor.h"
#include "pdfMetadata.h"
#include "pdfmem.h"
#include "pdfxref.h"

#include "namedef_.h"
#include "hqmemcmp.h"
#include "swerrors.h"
#include "monitor.h"

/** Returns to TRUE if a feature of PDF 1.5 is not allowed for the passed
enforcement level.
*/
static Bool pdf1_5FeatureBlocked(uint32 enforcement)
{
  return (enforcement & (PDFX_1_3_BASED | PDFX_1_4_BASED)) != 0;
}

/** Conformance Level: All

Unrecognised operators are not permitted. Returns false on error.
*/
Bool pdfxUnknownOperator(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  return pdfxError(ixc, PDFXERR_INVALID_OPERATOR);
}

/** Conformance Level: All

Preseparated data is not allowed in PDF/X. Returns false on error.
*/
Bool pdfxPreseparatedJob(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  return pdfxError(ixc, PDFXERR_PRESEP);
}

/** Conformance Level: All

Actions are not permitted. Returns false on error.
*/
Bool pdfxActionDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  return pdfxError(ixc, PDFXERR_ACTIONS);
}

/** Conformance Level: Various

Soft masked images are not permitted prior to X-4.
*/
Bool pdfxSoftMaskedImageDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_AND_X3) != 0)
    return pdfxError(ixc, PDFXERR_SOFT_MASKED_IMAGE);

  return TRUE;
}

/** Conformance Level: All

All fonts should be embedded in the PDF file for PDF/X. 'fileMissing', when
true, indicates that the font file is missing, otherwise it is assumed that
the font descriptor for a type 3 font is missing.

Returns false on error.
*/
Bool pdfxExternalFontDetected(PDFCONTEXT *pdfc, Bool fileMissing)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  return pdfxError(ixc, fileMissing ? PDFXERR_NO_FONTFILE :
                                      PDFXERR_NO_FONTDESCRIPTOR);
}

/** Conformance Level: All

OPI is not permitted. It is allowed in PDF/X-1, but we don't support that (we
only support PDF/X-1a).

Returns false on error.
*/
Bool pdfxOPIDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  return pdfxError(ixc, PDFXERR_OPI_KEY);
}

/** Conformance Level: X4

The 'F' key is not allowed in stream dictionaries.
*/
Bool pdfxFKeyInStreamDictionaryDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) != 0) {
    if (! pdfxError(ixc, PDFXERR_F_KEY_PROHIBITED))
      return FALSE;
  }

  return TRUE;
}

/**
 * Conformance Level: Pre-X4.
 *
 * File specifications are not in specs prior to X4.
 */
Bool pdfxFileSpecificationDetected(PDFCONTEXT *pdfc, Bool embeddedFile)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  /* File specs allowed in X5g or X5pg. */
  if ((ixc->conformance_pdf_version & (PDF_ENFORCE_X5g | PDF_ENFORCE_X5pg)) != 0) {
    return TRUE;
  }

  /* File specs are not allowed in any pre-X4 spec. They're allowed in X4 if the
  referred file is embedded. */
  if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_AND_X3) != 0) {
    if (! pdfxError(ixc, PDFXERR_FILESPEC))
      return FALSE;
  }

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) != 0 && ! embeddedFile) {
    if (! pdfxError(ixc, PDFXERR_FILESPEC_NOT_EMBEDDED))
      return FALSE;
  }

  return pdfxError(ixc, PDFXERR_FILESPEC);
}

/**
 * Conformance Level: Pre-X5, X5n.
 *
 * Reference XObjects are disallowed by all pre-X5 specs (either explicitly or by
 * the version of PDF used), and by X5n.
 */
Bool pdfxReferenceXObjectDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if ((ixc->conformance_pdf_version & PDF_CHECK_EXTERNAL_GRAPHICS_ALLOWED) == 0) {
    return pdfxError(ixc, PDFXERR_REFERENCEXOBJECT);
  }

  return TRUE;
}

/** Conformance Level: All

Halftones may not specify the HalftoneName key.
*/
Bool pdfxHalftoneNameDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  return pdfxError(ixc, PDFXERR_HALFTONENAME_KEY);
}

/** Conformance Level: All

The 'PS' operator is not permitted.
*/
Bool pdfxPSDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  return pdfxError(ixc, PDFXERR_INVALID_PS_OPERATOR);
}

/** Conformance Level: All

Postscript XObjects are not permitted.
*/
Bool pdfxPSXObjectDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  return pdfxError(ixc, PDFXERR_PS_XOBJECT);
}

/** Conformance Level: All

16 bit images are a feature of PDF 1.5, and therefore not permitted for PDF/X
versions based on an earlier PDF version.
*/
Bool pdfx16BitImageDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  if (pdf1_5FeatureBlocked(ixc->conformance_pdf_version))
    return pdfxError(ixc, PDFXERR_16_BIT_IMAGE);

  return TRUE;
}

/** Conformance Level: Various.

Object streams are a PDF 1.5 feature, and therefore not permitted for PDF/X
versions based on an earlier PDF version.
*/
Bool pdfxObjectStreamDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  if (pdf1_5FeatureBlocked(ixc->conformance_pdf_version))
    return pdfxError(ixc, PDFXERR_OBJECT_STREAM);

  return TRUE;
}

/** Conformance Level: All

Cross-reference streams are a feature of PDF 1.5, and therefore not permitted
for PDF/X versions based on an earlier PDF version.
*/
Bool pdfxXrefStreamDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  if (pdf1_5FeatureBlocked(ixc->conformance_pdf_version))
    return pdfxError(ixc, PDFXERR_XREF_STREAM);

  return TRUE;
}

/** Conformance Level: All

Optional content is a feature of PDF 1.5, and therefore not permitted for PDF/X
versions based on an earlier PDF version.
*/
Bool pdfxOptionalContentDetected(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  if (pdf1_5FeatureBlocked(ixc->conformance_pdf_version))
    return pdfxError(ixc, PDFXERR_OPTIONAL_CONTENT);

  return TRUE;
}

/** Conformance Level: X4/X4p

Alternate presentations are not allowed in X4 specs.
*/
Bool pdfxPresStepsDictionaryDetected(PDFCONTEXT* pdfc)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) == 0)
    return TRUE;

  return pdfxError(ixc, PDFXERR_PRESSTEPS);
}

/**
 * Conformance Level: Various
 *
 * External output profiles are only allowed in X4p, X5n, and X5pg.
 * \param internalProfileFound true if an internal profile was found in
 *        addition to the external profile.
 */
Bool pdfxExternalOutputProfileDetected(PDFCONTEXT* pdfc,
                                       Bool internalProfileFound)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_EXTERNAL_PROFILE_ALLOWED) != 0) {
    /* It's ok to have an external profile, but not an internal one too. */
    if (internalProfileFound) {
      return pdfxError(ixc, PDFXERR_INTERNAL_AND_EXTERNAL_PROFILES);
    }
    return TRUE;
  }

  return pdfxError(ixc, PDFXERR_EXTERNAL_PROFILE_FORBIDDEN);
}

/** Conformance Level: All

Not all halftone types are permitted.
*/
Bool pdfxCheckHalftoneType(PDFCONTEXT *pdfc, int32 type)
{
  GET_PDFXC_AND_IXC;

  /* Only types 1 and 5 are permitted. */
  if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_AND_X3) != 0) {
    if (type != 1 && type != 5)
      if (! pdfxError(ixc, PDFXERR_HALFTONE_TYPE))
        return FALSE;
  }
  return TRUE;
}

/** Conformance Level: All

Check that Image XObjects listed in the /Alternates array from the main image
dictionary do not have their DefaultForPrinting set to true.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns FALSE if a C error occured.
*/
Bool pdfxCheckImageAlternates(PDFCONTEXT *pdfc, OBJECT *alternatesArray)
{
  static NAMETYPEMATCH pdf_alternates_dict[] = {
  /* 0 */ { NAME_DefaultForPrinting | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
            DUMMY_END_MATCH
  } ;

  int32 i;
  GET_PDFXC_AND_IXC;

  /* Is the check required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  HQASSERT(alternatesArray != NULL, "pdfx_image_check_alternates - "
           "alternatesArray null in pdf_check_alternates.");

  /* For each alternate image XObject mentioned in the array */
  for (i = 0; i < theLen(*alternatesArray); i ++) {
    OBJECT *pObj = &(oArray(*alternatesArray)[i]);
    if (pObj != NULL) {
      HQASSERT(oType(*pObj) == ODICTIONARY, "pdfx_image_check_alternates - "
               "Image Alternates entry not a dictionary.");

      if (pdf_dictmatch(pdfc, pObj, pdf_alternates_dict)) {
        pObj = pdf_alternates_dict[0].result;
        if (pObj != NULL && oBool(*pObj)) {
          if (! pdfxError(ixc, PDFXERR_ALTERNATE_FOR_PRINTING))
            return FALSE;
        }
      }
    }
  }
  return TRUE;
}

/** Conformance Level: Various

Only certain filters types are prohibited in some pdf/x versions.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns FALSE if a C error occured.
*/
Bool pdfxCheckFilter(PDFCONTEXT *pdfc, FILELIST *flptr, STACK *stack)
{
  Bool error = FALSE;
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  /* Crypt filters are not allowed, expressly by X4 and because of the PDF
  version they appeared in (1.5) for all other variants. */
  if (HqMemCmp(flptr->clist, flptr->len, NAME_AND_LENGTH("Crypt")) == 0)
    error = TRUE;

  /* LZW is only allowed in version x1a variants. */
  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X1a) == 0 &&
      HqMemCmp(flptr->clist, flptr->len, NAME_AND_LENGTH("LZWDecode")) == 0)
    error = TRUE;

  if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_AND_X3) != 0) {
    /* X1a/X3:2003 prohibits JBIG2, which is not available to X3:2002 either
    because it's only in PDF 1.4. JPEG2k is a 1.5 feature. */
    if (HqMemCmp(flptr->clist, flptr->len, NAME_AND_LENGTH("JBIG2Decode")) == 0 ||
        HqMemCmp(flptr->clist, flptr->len, NAME_AND_LENGTH("JPXDecode")) == 0) {
      error = TRUE;
    }

    if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_2001_ONLY) != 0) {
      PDF_IMC_PARAMS *imc ;
      imc = pdfc->u.i;
      if (imc != NULL && !imc->handlingImage) {
        if (HqMemCmp(flptr->clist, flptr->len,
                     NAME_AND_LENGTH("CCITTFaxDecode")) == 0 ||
            HqMemCmp(flptr->clist, flptr->len,
                     NAME_AND_LENGTH("DCTDecode")) == 0) {
          error = TRUE;
        }
      }
    }
  }

  if (error) {
    if (! pdfxError(ixc, PDFXERR_FILE_COMPRESSION)) {
      pop(stack);
      return FALSE;
    }
  }

  return TRUE;
}

/** Conformance Level: All

'Trapped' must be present in an info dictionary, and must be True or False.
*/
Bool pdfxCheckTrapped(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (ixc->infoTrapped == INFO_TRAPPED_NONE) {
    if (! pdfxError(ixc, PDFXERR_NO_TRAPPED_KEY))
      return FALSE;
  }
  else {
    /* The value of the Trapped key must be either True or False. */
    if (ixc->infoTrapped != INFO_TRAPPED_TRUE &&
        ixc->infoTrapped != INFO_TRAPPED_FALSE) {
      if (! pdfxError(ixc, PDFXERR_TRAPPED_UNKNOWN))
        return FALSE;
    }
  }
  return TRUE;
}

/** Conformance Level: All

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if a C error occured.
*/
Bool pdfxCheckTrapNet(PDFCONTEXT *pdfc, OBJECT *pDictObj)
{
  /* The TrapNet annotation dictionary - we only need to check the
  FontFauxing key. */
  static NAMETYPEMATCH pdf_trapnet_dict[] = {
  /* 0 */ {NAME_Subtype, 2, {ONAME, OINDIRECT}},
  /* 1 */ {NAME_FontFauxing | OOPTIONAL, 3, {OARRAY, OPACKEDARRAY, OINDIRECT}},
          DUMMY_END_MATCH};
  OBJECT *pObj;
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  /* Given that we have a TrapNet annotation, then the value of the Trapped key
  in the Info Dictionary should be True. The latter was determined earlier in
  pdf_walk_infodict and saved in ixc->infoTrapped. */
  if (ixc->infoTrapped != INFO_TRAPPED_TRUE) {
    if (! pdfxError(ixc, PDFXERR_INFO_TRAPPED_NOT_TRUE))
      return FALSE;
  }

  /* Read the TrapNet annotation dictionary. */
  if (! pdf_dictmatch(pdfc, pDictObj, pdf_trapnet_dict))
    return FALSE;

  /* Double check that the SubType key is present and indicates TrapNet. */
  pObj = pdf_trapnet_dict[0].result;
  if (oNameNumber(*pObj) != NAME_TrapNet) {
    HQFAIL("TrapNet annotation has wrong dictionary?");
    return error_handler(UNDEFINED);
  }

  /* The FontFauxing key must either be absent or an empty array. */
  if (pdf_trapnet_dict[1].result != NULL) {
    if (theLen(* pdf_trapnet_dict[1].result ) != 0)
      if (! pdfxError( ixc, PDFXERR_TRAPNET_FONTFAUXING ))
        return FALSE;
  }

  return TRUE;
}

/** Conformance Level: All

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if a C error occured.
*/
Bool pdfxCheckAnnotation(PDFCONTEXT *pdfc, OBJECT *annotationDict)
{
  static NAMETYPEMATCH annotationMatch[] = {
    /* 0 */ {NAME_A | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    /* 1 */ {NAME_AA | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
            DUMMY_END_MATCH};
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, annotationDict, annotationMatch))
    return FALSE;

  /* Actions (and therefor trigger events) are not permitted in an
  annotation. */
  if (annotationMatch[0].result != NULL  || annotationMatch[1].result != NULL) {
    if (! pdfxError(ixc, PDFXERR_ACTIONS))
      return FALSE;
  }

  return TRUE;
}

/** Conformance Level: All

Different versions of PDF/X place different restrictions on the PDF page
boxes. Note that this function must be called after all Page dictionaries
in a single hierarchy have been parsed (to honor inheritance).

  (a) MediaBox must be present, and include all subsequent bounds;
  (b) Either TrimBox or ArtBox [but not both] must be present;
  (c) (TrimBox or ArtBox) < BleedBox [if BleedBox present];
  (d) (TrimBox or ArtBox) < CropBox [if CropBox present];
  (f) All other boxes are within the Media Box;

This function also retains some of the bounding boxs in the ixc for later use
in pdfxCheckAnnotationBounds().

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if a C error occured.
*/
Bool pdfxCheckPageBounds(PDFCONTEXT *pdfc, PDF_PAGEDEV *pagedev)
{
  OBJECT *art_trim_box = NULL;
  sbbox_t currentBox;
  GET_PDFXC_AND_IXC;

  /* Flag as 'not set' */
  bbox_clear(&ixc->pdfxState.pageLimitBounds);
  bbox_clear(&ixc->pdfxState.innerPageBounds);

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (ixc->pdfxState.usingBleedBoxInViewerPrefs && pagedev->BleedBox == NULL) {
    if (! pdfxError(ixc, PDFXERR_VIEWER_PREFS_BLEEDBOX_MISSING))
      return FALSE;
  }

  /* The Media box is always required by the PDF spec, but we don't insist on
  it when processing the page object - insist on it here. */
  if (pagedev->MediaBox == NULL)
    if (! pdfxError(ixc, PDFXERR_NO_MEDIABOX))
      return FALSE;

  /* One of (but not both) Art box or Trim box must be defined. */
  if (pagedev->ArtBox != NULL)
    art_trim_box = pagedev->ArtBox;

  if (pagedev->TrimBox != NULL) {
    if (art_trim_box != NULL)
      if (! pdfxError(ixc, PDFXERR_ARTANDTRIM))
        return FALSE;

    art_trim_box = pagedev->TrimBox;
  }

  /* If the art/trim box was absent, give up. Otherwise, check for the various
  containing boxes and verify that each contains the predecessor. */
  if (art_trim_box == NULL) {
    if (! pdfxError(ixc, PDFXERR_NO_ARTORTRIM))
      return FALSE;
  }
  else {
    Bool ret = TRUE;

    /* The currentBox starts as the art/trim box. The is then checked against
    the containing box, which them becomes the current box, and so on. */
    if (! object_get_bbox(art_trim_box, &currentBox))
      return FALSE;

    /* Initialise the page bounds in the ixc which we'll use later in
    pdfxCheckAnnotationBounds(). */
    ixc->pdfxState.innerPageBounds = ixc->pdfxState.pageLimitBounds = currentBox;

    /* The art/trim box must be within the bleed box, if present. */
    if (pagedev->BleedBox != NULL) {
      sbbox_t bleedbbox;

      if (! object_get_bbox(pagedev->BleedBox, &bleedbbox))
        return FALSE;

      /* Update the page limit bounds; this stops growing at the Bleed box. */
      ixc->pdfxState.pageLimitBounds = bleedbbox;

      if (! bbox_contains(&bleedbbox, &currentBox))
        ret = pdfxError(ixc, PDFXERR_TA_BOX_NOTIN_BLEEDBOX);

      currentBox = bleedbbox;
    }

    /* The bleed box (or whatever) must be within the Crop Box. */
    if (pagedev->CropBox != NULL) {
      sbbox_t cropbbox;

      if (! object_get_bbox(pagedev->CropBox, &cropbbox))
        return FALSE;

      if (! bbox_contains(&cropbbox, &currentBox))
        ret = pdfxError(ixc, PDFXERR_BTA_BOX_NOTIN_CROPBOX);

      currentBox = cropbbox;
    }

    /* The crop box (or whatever) must be within the Media Box. */
    if (pagedev->MediaBox != NULL) {
      sbbox_t mediaBox;

      if (! object_get_bbox(pagedev->MediaBox, &mediaBox))
        return FALSE;

      if (! bbox_contains(&mediaBox, &currentBox))
        ret = pdfxError(ixc, PDFXERR_CBTA_BOX_NOTIN_MEDIABOX);
    }

    if (! ret)
      return FALSE;
  }

  return TRUE;
}

/** Conformance Level: All

PDF/X requires all annotations (except Trapping annotations) to lie outside the
BleedBox (or ArtBox or TrimBox - read the spec.!) of the page, except for
PrinterMark annotations, which must lie outside of the TrimBox.

The BleedBox and Art/Trim Box (or whatever) will have been retained in
ixc->PageLimitBounds and ixc->InnerPageBounds in pdfxCheckPageBounds().

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if a C error occured.
*/
Bool pdfxCheckAnnotationBounds(PDFCONTEXT *pdfc, OBJECT* annotationType,
                               OBJECT* annotationRectangle)
{
  sbbox_t annotationBounds;
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (! object_get_bbox(annotationRectangle, &annotationBounds))
    return FALSE ;

  switch (oNameNumber(*annotationType)) {
    /* Most annotations must lie outside of the page limit bounds (which is one
    of the Bleed/Art/Trim Boxes). */
    default:
      if (! bbox_is_empty(&ixc->pdfxState.pageLimitBounds)) {
        /* Intersection test is not exclusive; degenerate intersecting edges
        are included. */
        if (bbox_intersects(&annotationBounds, &ixc->pdfxState.pageLimitBounds)) {
          if (! pdfxError(ixc, PDFXERR_ANNOT_ON_PAGE))
            return FALSE;
        }
      }
      break;

    case NAME_TrapNet:
      /* Trap net's bounds are not restricted. */
      break;

    case NAME_PrinterMark:
      /* PrinterMark annotations must be outside of the art/trim box. These are
      a feature of PDF 1.4, and are thus not allowed for PDF/X versions based
      on 1.3. */
      if ((ixc->conformance_pdf_version & PDFX_1_3_BASED) != 0)
        if (! pdfxError(ixc, PDFXERR_PRINTERMARKS_NOT_IN_1_3))
          return FALSE;

      if (! bbox_is_empty(&ixc->pdfxState.innerPageBounds)) {
        /* Intersection test is not exclusive; degenerate intersecting edges
        are included. */
        if (bbox_intersects(&annotationBounds, &ixc->pdfxState.innerPageBounds)) {
          if (! pdfxError(ixc, PDFXERR_PRINTERMARK_ANNOT_ON_PAGE))
            return FALSE;
        }
      }
      break;
  }

  return TRUE;
}

/** Conformance Level: Various

Check that the passed colorspace is acceptable as a primary colorspace (i.e. one
passed to cs/CS).

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if a C error occured.
*/
static Bool checkColorSpace(PDF_IXC_PARAMS *ixc, int32 cspace)
{
  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X1a) != 0) {
    switch (cspace) {
    case NAME_DeviceGray:
    case NAME_DeviceCMYK:
      /* These color spaces are directly allowed. */
      break ;

    case NAME_Pattern:
    case NAME_Indexed:
    case NAME_Separation:
    case NAME_DeviceN:
      /* These spaces are allowed, but must (and will) have their alternative
         spaces validated too. */
      break;

    default:
      if (! pdfxError(ixc, PDFXERR_BAD_X1a_COLORSPACE))
        return FALSE;
      break;
    }
  } else if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X3) != 0) {
    switch (cspace) {
    case NAME_DeviceGray:
    case NAME_DeviceCMYK:
    case NAME_DeviceRGB:
      /* These color spaces are directly allowed. */
      break;
    case NAME_CalRGB:
    case NAME_CalGray:
    case NAME_Lab:
    case NAME_ICCBased:
      /* These color spaces are allowed if a DestOutputProfile is present. */
      if (!ixc->PDFXOutputProfilePresent) {
        if (! pdfxError(ixc, PDFXERR_NO_DEST_OUTPUT_PROFILE))
          return FALSE;
      }
      break ;

    case NAME_Pattern:
    case NAME_Indexed:
    case NAME_Separation:
    case NAME_DeviceN:
      /* These spaces are allowed, but must (and will) have their alternative
         spaces validated too. */
      break;

    default:
      if (! pdfxError(ixc, PDFXERR_BAD_X3_COLORSPACE))
        return FALSE;
      break;
    }
  }

  return TRUE;
}

/** Conformance Level: Various

Check that the passed colorspace is acceptable as an alternate/base colorspace
(e.g. for an indexed color space).

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if a C error occured.
*/
static Bool checkAlternateColorSpace(PDF_IXC_PARAMS *ixc, int32 cspace,
                                     int32 parentcspace)
{
  HQASSERT(parentcspace == NAME_Indexed || parentcspace == NAME_Pattern ||
           parentcspace == NAME_Separation || parentcspace == NAME_DeviceN,
           "pdfxCheckAlternateColorSpace - Unexpected parent color space");

  /* Indexed and Pattern color spaces refer to a 'base' color space, while
     Separation and DeviceN spaces refer to an 'alternative' color space. */
  if (parentcspace == NAME_Indexed || parentcspace == NAME_Pattern) {
    switch (cspace) {
    case NAME_DeviceGray:
    case NAME_DeviceCMYK:
      /* As 'base' spaces, these are ok but (for X-1:1999) must (and will) be
         intercepted as ICCBased spaces via the "Default" mechanism. */
      break;
    case NAME_Separation:     /* allowed */
      break;
    case NAME_DeviceN:        /* allowed in X-1a and X3 */
      break;
    case NAME_DeviceRGB:
    case NAME_Lab:
    case NAME_CalGray:
    case NAME_CalRGB:
    case NAME_ICCBased:
      /* These are valid 'bases' for X-3 only */
      if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X3) != 0) {
        /* ... but only if a DestOutputProfile is present. */
        if (!ixc->PDFXOutputProfilePresent) {
          if (! pdfxError(ixc, PDFXERR_NO_DEST_OUTPUT_PROFILE))
            return FALSE;
        }
        break;
      }
      /* ** FALL THRU ** */
    default:
      if (! pdfxError(ixc, PDFXERR_BASE_COLORSPACE))
        return FALSE;
      break;
    }
  } else {/* Parent colorspace should be Separation or DeviceN */
    switch (cspace) {
    case NAME_DeviceGray:
    case NAME_DeviceCMYK:
      /* As 'alternative' spaces, these are ok but (for X-1:1999) must (and
         will) be intercepted as ICCBased spaces via the "Default" mechanism.*/
      break ;

    case NAME_DeviceRGB:
    case NAME_Lab:
    case NAME_CalGray:
    case NAME_CalRGB:
    case NAME_ICCBased:
      /* These are valid 'alternatives' for X-3 only */
      if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X3) != 0) {
        /* ... but only if a DestOutputProfile is present. */
        if (!ixc->PDFXOutputProfilePresent) {
          if (! pdfxError(ixc, PDFXERR_NO_DEST_OUTPUT_PROFILE))
            return FALSE;
        }
        break;
      }
      /* ** FALL THRU ** */
    default:
      if (! pdfxError(ixc, PDFXERR_ALTERNATIVE_COLORSPACE))
        return FALSE;
      break;
    }
  }

  return TRUE;
}

/** Conformance Level: Various

Colorspaces are limited in PDF/X. The parent space name should be
NULL_COLORSPACE when not available, or a valid colorspace name when spaceName
is an alternate colorspace (e.g. the base space for an /Index colorspace).

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if a C error occured.
*/
Bool pdfxCheckColorSpace(PDFCONTEXT *pdfc, int32 spaceName,
                         int32 parentSpaceName)
{
  GET_PDFXC_AND_IXC;

  /* Is checking required. */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  /*  If the parent space is NULL_COLORSPACE, then we are checking a
  directly-specified colorspace, otherwise it's an alternate. */
  if (parentSpaceName == NULL_COLORSPACE) {
    if (! checkColorSpace(ixc, spaceName))
      return FALSE;
  }
  else {
    if (! checkAlternateColorSpace(ixc, spaceName, parentSpaceName))
      return FALSE;
  }

  return TRUE;
}

/** Conformance Level: X3 only.

Check the relevant Default colorspace for conformance with PDF/X. No check is
made to confirm that the default is sensible (i.e. CalGray is not a sensible
default for DeviceCMYK) - that is left to the regular PDF code.
*/
Bool pdfxCheckDefaultColorSpace(PDFCONTEXT *pdfc, int32 colorSpaceName)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X3) == 0)
    return TRUE;

  /* Only colorimetric colorspaces are allowed, i.e. ICCBased, CalGray and
  CalRGB are allowed. No device colorspace maps sensibly onto LAB. */
  if (colorSpaceName != NAME_ICCBased && colorSpaceName != NAME_CalGray &&
      colorSpaceName != NAME_CalRGB) {
    if (! pdfxError(ixc, PDFXERR_NOT_VALID_DEFAULT_SPACE))
      return FALSE;
  }

  return TRUE;
}

/** Conformance Level: X1 only.

The rg/RG operators implicitly set the colorspace to DeviceRGB, which is not an
allowed colorspace in X1. 'stroking' should be true when the stroking variant of
the rg operator is used ('RG').
*/
Bool pdfxCheckRgOperator(PDFCONTEXT *pdfc, Bool stroking)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X1a) != 0) {
    if (! pdfxError( ixc, stroking ? PDFXERR_INVALID_RG_OPERATOR :
                                     PDFXERR_INVALID_rg_OPERATOR))
      return FALSE;
  }
  return TRUE;
}

/** Conformance Level: All.

In the file trailer dictionary, the Encrypt entry is not permitted, and the ID
entry is required.
*/
Bool pdfxCheckTrailerDictionary(PDFCONTEXT *pdfc, OBJECT *trailerDict)
{
  NAMETYPEMATCH trailerDictMatch[] = {
/* 0 */ {NAME_Encrypt | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
/* 1 */ {NAME_ID      | OOPTIONAL, 3, {OARRAY, OPACKEDARRAY, OINDIRECT}},
        DUMMY_END_MATCH};
  GET_PDFXC_AND_IXC;

  HQASSERT(trailerDict != NULL, "checkEncryptDictionary - trailer dictionary "
           "cannot be null.");

  /* Is checking required. */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, trailerDict, trailerDictMatch))
    return FALSE;

  /* Encrypt is prohibited. */
  if (trailerDictMatch[0].result != NULL)
    if (! pdfxError(ixc, PDFXERR_ENCRYPT_KEY))
      return FALSE;

  /* ID is required. */
  if (trailerDictMatch[1].result == NULL)
    if (! pdfxError(ixc, PDFXERR_TRAILER_ID))
      return FALSE;

  return TRUE;
}

/** Conformance Level: Various

Transparency features of PDF 1.4 are not allowed to be used in PDF 1.3 based
conformance levels, and are restricted in 1.4 based conformance levels such that
partial transparency is not possible.
*/
Bool pdfxCheckExtGState(PDFCONTEXT *pdfc, OBJECT *extGStateDict)
{
  static NAMETYPEMATCH gstateMatch[] = {
/* 0 */ {NAME_ca | OOPTIONAL, 3, {OREAL, OINTEGER, OINDIRECT}},
/* 1 */ {NAME_CA | OOPTIONAL, 3, {OREAL, OINTEGER, OINDIRECT}},
/* 2 */ {NAME_SMask | OOPTIONAL, 3, {ONAME, ODICTIONARY, OINDIRECT}},
/* 3 */ {NAME_BM | OOPTIONAL, 4, {ONAME, OARRAY, OPACKEDARRAY, OINDIRECT}},
/* 4 */ {NAME_TK | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
/* 5 */ {NAME_AIS | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
/* 6 */ {NAME_TR | OOPTIONAL, 6, {ONAME, ODICTIONARY, OFILE, OARRAY,
                                  OPACKEDARRAY, OINDIRECT}},
/* 7 */ {NAME_TR2 | OOPTIONAL, 6, {ONAME, ODICTIONARY, OFILE, OARRAY,
                                   OPACKEDARRAY, OINDIRECT}},
/* 8 */ {NAME_HTP | OOPTIONAL, 3, {OARRAY, OPACKEDARRAY, OINDIRECT}},
        DUMMY_END_MATCH};
  GET_PDFXC_AND_IXC;

  HQASSERT(extGStateDict != NULL, "pdfxCheckExtGState - extGStateDict cannot "
           "be null.");

  /* Is checking required. */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, extGStateDict, gstateMatch))
    return FALSE;

  /* TR is not allowed in any version. */
  if (gstateMatch[6].result != NULL) {
    if (! pdfxError(ixc, PDFXERR_TR))
      return FALSE;
  }

  /* TR2 is only allowed in X4 and above, and then can only be /Default. */
  if (gstateMatch[7].result != NULL) {
    if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_AND_X3) != 0) {
      if (! pdfxError(ixc, PDFXERR_TR2))
        return FALSE;
    }
    else {
      if (oType(*gstateMatch[7].result) != ONAME ||
          oNameNumber(*gstateMatch[7].result) != NAME_Default) {
        if (! pdfxError(ixc, PDFXERR_TR2_INVALID))
          return FALSE;
      }
    }
  }

  /* HTP is not allowed. Note that this key is not listed in the PDF spec... */
  if (gstateMatch[8].result != NULL) {
    if (! pdfxError(ixc, PDFXERR_HTP))
      return FALSE;
  }

  if ((ixc->conformance_pdf_version & PDFX_1_3_BASED) != 0) {
    /* A 1.3 based PDF/X file should not be accessing transparency features of
    the GSTATE. */
    int i;
    for (i = 0; i <= 5; i ++) {
      if (gstateMatch[i].result != NULL) {
        if (! pdfxError(ixc, PDFXERR_1_3_BASED_USING_1_4_GSTATE))
          return FALSE;
        break;
      }
    }
  }
  else {
    /* Versions prior to X-4 are not allowed to introduce transparency. */
    if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_AND_X3) != 0) {
      /* The Constant alpha is only allowed to be 1.0. */
      SYSTEMVALUE ca = 1, CA = 1;
      if (gstateMatch[0].result != NULL)
        ca = object_numeric_value(gstateMatch[0].result);
      if (gstateMatch[1].result != NULL)
        CA = object_numeric_value(gstateMatch[1].result);
      /* We'll perform an exact check here; the file is not meant to contain any
      value for ca or CA other than 1.0. */
      if (ca != 1.0 || CA != 1.0)
        if (! pdfxError(ixc, PDFXERR_INVALID_CONSTANT_ALPHA))
          return FALSE;

      /* SMask must be 'None'. */
      if (gstateMatch[2].result != NULL) {
        if (oType(*gstateMatch[2].result) != ONAME ||
            oNameNumber(*gstateMatch[2].result) != NAME_None)
          if (! pdfxError(ixc, PDFXERR_INVALID_SOFT_MASK))
            return FALSE;
      }

      /* BM must be 'Normal' or 'Compatible'. */
      if (gstateMatch[3].result != NULL) {
        int32 mode;
        /* The blend mode can be an array of names or a single name. */
        if (oType(*gstateMatch[3].result) != ONAME) {
          if (oType(oArray(*gstateMatch[3].result)[0]) != ONAME)
            return error_handler(TYPECHECK);
          else
            mode = oNameNumber(oArray(*gstateMatch[3].result)[0]);
        }
        else
          mode = oNameNumber(*gstateMatch[3].result);

        if (mode != NAME_Normal && mode != NAME_Compatible)
          if (! pdfxError(ixc, PDFXERR_INVALID_BLEND_MODE))
            return FALSE;
      }
    }
  }
  return TRUE;
}

/** Conformance Level: Various

Prior to X4, transparency group XObjects are not permitted.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if an error occured.
*/
Bool pdfxCheckTransparencyGroup(PDFCONTEXT *pdfc, OBJECT *groupDictionary) {
  GET_PDFXC_AND_IXC;

  UNUSED_PARAM(OBJECT*, groupDictionary);

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (ixc->conformance_pdf_version & PDF_CHECK_TRANSPARENCY_FORBIDDEN)
    return pdfxError(ixc, PDFXERR_TRANSPARENCY_GROUP);
  else
    return TRUE;
}

/** Conformance Level: X4 and above.

If the page color space is not specified and the document is PDF/X-4, we
have to use the device space of the output condition.

\param pageGroupColorSpace The page group color space. If this is null, the
passed overrideColorSpace will be set if required.

\param overrideColorSpace This will be set to the override color space name if
required, otherwise will not be changed.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if an error occured.
*/
Bool pdfxCheckForPageGroupCSOverride(PDFCONTEXT *pdfc,
                                     OBJECT* pageGroupColorSpace,
                                     OBJECT* overrideColorSpace)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) == 0)
    return TRUE;

  if (pageGroupColorSpace == NULL) {
    switch (ixc->pdfxState.conditionDeviceSpace) {
      default:
        if (! pdfxError(ixc, PDFXERR_INVALID_OUTPUTCONDITION_DEVICE_CS))
          return FALSE;
        return TRUE;

      case SPACE_DeviceGray:
        object_store_name(overrideColorSpace, NAME_DeviceGray, LITERAL);
        break;

      case SPACE_DeviceRGB:
        object_store_name(overrideColorSpace, NAME_DeviceRGB, LITERAL);
        break;

      case SPACE_DeviceCMYK:
        object_store_name(overrideColorSpace, NAME_DeviceCMYK, LITERAL);
        break;
    }
  }

  return TRUE;
}

/** Conformance Level: Various

For X4, Permission dictionaries can only contain UR and UR3.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if an error occured.
*/
#define PERMISSIONS_DICT_KEYS 2

Bool pdfxCheckPermissionsDictionary(PDFCONTEXT *pdfc, OBJECT *permDictionary)
{
  NAMETYPEMATCH permissionsDictMatch[PERMISSIONS_DICT_KEYS + 1] = {
    {NAME_UR | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    {NAME_UR3 | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    DUMMY_END_MATCH};
  int32 i, dictLength;
  int32 foundKeys = 0;
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, permDictionary, permissionsDictMatch))
    return FALSE;

  for (i = 0; i < PERMISSIONS_DICT_KEYS; i ++) {
    if (permissionsDictMatch[i].result != NULL)
      foundKeys ++;
  }

  getDictLength(dictLength, permDictionary);
  if (dictLength != foundKeys)
    return pdfxError(ixc, PDFXERR_INVALID_PERMISSIONS_DICT);
  else
    return TRUE;
}

/** Conformance Level: Various

Both X1a:2001 and X4 (but not the others oddly) place restrictions on the
ViewerPreferences dictionary in the page catalog.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if an error occured.
*/
#define VIEWER_PREFS_DICT_KEYS 4

Bool pdfxCheckViewerPreferencesDictionary(PDFCONTEXT *pdfc,
                                          OBJECT *vPrefsDictionary)
{
  NAMETYPEMATCH viewerPrefsDictMatch[VIEWER_PREFS_DICT_KEYS + 1] = {
    {NAME_ViewArea | OOPTIONAL, 2, {ONAME, OINDIRECT}},
    {NAME_ViewClip | OOPTIONAL, 2, {ONAME, OINDIRECT}},
    {NAME_PrintArea | OOPTIONAL, 2, {ONAME, OINDIRECT}},
    {NAME_PrintClip | OOPTIONAL, 2, {ONAME, OINDIRECT}},
    DUMMY_END_MATCH};
  int32 i;
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version &
       (PDF_ENFORCE_X1a_2003 | PDF_CHECK_ANY_X4)) == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, vPrefsDictionary, viewerPrefsDictMatch))
    return FALSE;

  for (i = 0; i < VIEWER_PREFS_DICT_KEYS; i ++) {
    OBJECT* value = viewerPrefsDictMatch[i].result;

    if (value != NULL) {
      if (oNameNumber(*value) != NAME_MediaBox &&
          oNameNumber(*value) != NAME_BleedBox)
        return pdfxError(ixc, PDFXERR_INVALID_VIEWER_PREFS_DICT);

      if (oNameNumber(*value) == NAME_BleedBox)
        ixc->pdfxState.usingBleedBoxInViewerPrefs = TRUE;
    }
  }

  return TRUE;
}


/** Conformance Level: X4/X4p

AlternatePresentations are not allowed in the name dictionary.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if an error occured.
*/
Bool pdfxCheckNameDictionary(PDFCONTEXT* pdfc, OBJECT* nameDictionary)
{
  NAMETYPEMATCH nameMatches[] = {
    {NAME_AlternatePresentations | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    DUMMY_END_MATCH
  };
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, nameDictionary, nameMatches))
    return FALSE;

  if (nameMatches[0].result != NULL)
    return pdfxError(ixc, PDFXERR_ALTERNATE_PRESENTATIONS);

  return TRUE;
}

/** Check that the passed optional content configuration dictionary meets
PDF/X-4 conformance.
*/
static Bool checkOCConfigDictionary(PDFCONTEXT* pdfc,
                                    OBJECT* ocConfigDictionary)
{
  enum {
    nameMatch = 0,
    asMatch = 1
  };
  NAMETYPEMATCH ocConfigMatches[] = {
    {NAME_Name | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    {NAME_AS | OOPTIONAL, 2, {OARRAY, OINDIRECT}},
    DUMMY_END_MATCH
  };
  GET_PDFXC_AND_IXC;

  if (! pdf_dictmatch(pdfc, ocConfigDictionary, ocConfigMatches))
    return FALSE;

  if (ocConfigMatches[nameMatch].result == NULL) {
    if (! pdfxError(ixc, PDFXERR_OC_NAME_MISSING))
      return FALSE;
  }

  /* No mention on "BaseState" in PDF/X-4 2010 */

  /* No mention on "OFF" in PDF/X-4 2010 */

  /* "Order" does not need to be empty in PDF/X-4 2010 but does in
     2008 so ignore it. */

  /* Same rule applies to PDF/X-4:2008 & 2010 */
  if (ocConfigMatches[asMatch].result != NULL) {
    if (! pdfxError(ixc, PDFXERR_OC_AS_INVALID))
      return FALSE;
  }

  return TRUE;
}

/** Conformance Level: X4/X4p

There are some restrictions on the optional content properties
dictionary in the document catalog.

Only some partial checking is done because of differences between
X-4:2008 and X-4:2010 and we can't distinguish between those two
formats.

\return TRUE even if the conformance was not met (the error is
registered for handling elsewhere); returns FALSE if an error occured.
*/
Bool pdfxCheckOptionalContent(PDFCONTEXT* pdfc, OBJECT* ocPropsDictionary)
{
  enum {
    dMatch = 0,
    configsMatch = 1
  };
  NAMETYPEMATCH ocPropsMatches[] = {
    {NAME_D | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    {NAME_Configs | OOPTIONAL, 2, {OARRAY, OINDIRECT}},
    DUMMY_END_MATCH
  };
  OBJECT* configs = NULL;
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, ocPropsDictionary, ocPropsMatches))
    return FALSE;

  /* X-4:2010 does not state that D is required. */
  if (ocPropsMatches[dMatch].result != NULL) {
    OBJECT *D = ocPropsMatches[dMatch].result;
    if (oType(*D) == OINDIRECT)
      if (!pdf_lookupxref( pdfc, &D, oXRefID(*D),
                           theGen(*D), FALSE ))
        return FALSE;
    if (! checkOCConfigDictionary(pdfc, D))
      return FALSE;
  }

  configs = ocPropsMatches[configsMatch].result;
  if (configs != NULL) {
    int32 i, length;
    OBJECT* entry;
    if (oType(*configs) == OINDIRECT)
      if (!pdf_lookupxref( pdfc, &configs, oXRefID(*configs),
                           theGen(*configs), FALSE ))
        return FALSE;
    length = theLen(*configs);
    entry = oArray(*configs);
    for (i = 0; i < length; i ++) {
      OBJECT *resolvedEntry = entry;
      if (oType(*entry) == OINDIRECT)
        if (!pdf_lookupxref( pdfc, &resolvedEntry, oXRefID(*entry),
                             theGen(*entry), FALSE ))
          return FALSE;
      if (! checkOCConfigDictionary(pdfc, resolvedEntry))
        return FALSE;
      entry++;
    }
  }

  return TRUE;
}

/** Conformance Level: X4/X4p

The AcroForm dictionary in the document catalog cannot contain the XFA key.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if an error occured.
*/
Bool pdfxCheckAcroForm(PDFCONTEXT* pdfc, OBJECT* acroformDictionary)
{
  NAMETYPEMATCH acroformMatches[] = {
    {NAME_XFA | OOPTIONAL, 3, {OFILE, OARRAY, OINDIRECT}},
    DUMMY_END_MATCH
  };
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, acroformDictionary, acroformMatches))
    return FALSE;

  if (acroformMatches[0].result != NULL)
    return pdfxError(ixc, PDFXERR_XFA_KEY);
  else
    return TRUE;
}

/** Conformance Level: Various

There are various restrictions on the dictionaries within the document catalog.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if an error occured.
*/
Bool pdfxCheckCatalogDictionary(PDFCONTEXT* pdfc, OBJECT* catalogDictionary)
{
  enum {
    permsMatch = 0,
    vPrefsMatch = 1,
    namesMatch = 2,
    ocPropsMatch = 3,
    acroformMatch = 4
  };
  NAMETYPEMATCH catalogMatches[] = {
    {NAME_Perms | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    {NAME_ViewerPreferences | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    {NAME_Names | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    {NAME_OCProperties | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    {NAME_AcroForm | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    DUMMY_END_MATCH
  };
  GET_PDFXC_AND_IXC;

  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, catalogDictionary, catalogMatches))
    return FALSE;

  if (catalogMatches[permsMatch].result != NULL) {
    if (! pdfxCheckPermissionsDictionary(pdfc, catalogMatches[permsMatch].result))
      return FALSE ;
  }

  if (catalogMatches[vPrefsMatch].result != NULL) {
    if (! pdfxCheckViewerPreferencesDictionary(pdfc, catalogMatches[vPrefsMatch].result))
      return FALSE;
  }

  if (catalogMatches[namesMatch].result != NULL) {
    if (! pdfxCheckNameDictionary(pdfc, catalogMatches[namesMatch].result))
      return FALSE;
  }

  if (catalogMatches[ocPropsMatch].result != NULL) {
    if (! pdfxCheckOptionalContent(pdfc, catalogMatches[ocPropsMatch].result))
      return FALSE;
  }

  if (catalogMatches[acroformMatch].result != NULL) {
    if (! pdfxCheckAcroForm(pdfc, catalogMatches[acroformMatch].result))
      return FALSE;
  }

  return TRUE;
}

/** Conformance Level: X4/X4p

The rendering intent must be one of the four listed in the PDF specification.

\return true even if the conformance was not met (the error is registered for
handling elsewhere); returns false if an error occured.
*/
Bool pdfxCheckRenderingIntent(PDFCONTEXT* pdfc, OBJECT* intentName)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) == 0)
    return TRUE;

  if (oType(*intentName) != ONAME)
    return pdfxError(ixc, PDFXERR_INVALID_INTENT);

  switch (oNameNumber(*intentName)) {
    default:
      return pdfxError(ixc, PDFXERR_INVALID_INTENT);

    case NAME_AbsoluteColorimetric:
    case NAME_RelativeColorimetric:
    case NAME_Saturation:
    case NAME_Perceptual:
      break;
  }
  return TRUE;
}

/**
 * Conformance Level: X4/X4p
 *
 * All keys must be present in a DestOutputProfileRef dictionary. Additionally
 * it should not contain the ColorantTable key.
 */
Bool pdfxCheckDestOutputProfileRef(PDFCONTEXT* pdfc, OBJECT* refDictionary)
{
  enum {
    urlsMatch = 0,
    profileNameMatch = 1,
    checkSumMatch = 2,
    profileCSMatch = 3,
    iccVersionMatch = 4,
    colorantTableMatch = 5
  };
  NAMETYPEMATCH refMatch[] = {
    { NAME_URLs, 2, { OARRAY, OINDIRECT }},
    { NAME_ProfileName | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    { NAME_CheckSum | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    { NAME_ProfileCS | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    { NAME_ICCVersion | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    { NAME_ColorantTable | OOPTIONAL, 2, { OARRAY, OINDIRECT }},
    DUMMY_END_MATCH };
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (! pdf_dictmatch(pdfc, refDictionary, refMatch))
    return FALSE;

  /* The URLs array is vital; report that being missing explicitly. */
  if (refMatch[urlsMatch].result == NULL) {
    if (! pdfxError(ixc, PDFXERR_URLS_MISSING))
      return FALSE;
  }

  /* Other keys just get a catch-all message. */
  if (refMatch[profileNameMatch].result == NULL ||
      refMatch[checkSumMatch].result == NULL ||
      refMatch[profileCSMatch].result == NULL ||
      refMatch[iccVersionMatch].result == NULL) {
    if (! pdfxError(ixc, PDFXERR_PROFILE_REF_KEYS_MISSING))
      return FALSE;
  }

  if (refMatch[colorantTableMatch].result != NULL) {
    if (! pdfxError(ixc, PDFXERR_PROFILE_REF_CONTAINS_COLORANT_TABLE))
      return FALSE;
  }

  return TRUE;
}

/**
 * Conformance Level: Various
 *
 * Check that the passed dictionary is a URL specification, containing only
 * /FS and /F keys.
 */
Bool pdfxCheckUrl(PDFCONTEXT* pdfc, OBJECT* urlDictionary)
{
  enum {
    fsMatch = 0,
    fMatch = 1,
    efMatch = 2,
    dosMatch = 3,
    macMatch = 4,
    unixMatch = 5
  };
  NAMETYPEMATCH urlMatch[] = {
    { NAME_FS | OOPTIONAL, 2, { ONAME, OINDIRECT }},
    { NAME_F | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    { NAME_EF | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_DOS | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    { NAME_Mac | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    { NAME_Unix | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    DUMMY_END_MATCH };
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

 if (! pdf_dictmatch(pdfc, urlDictionary, urlMatch))
    return FALSE;

  if (urlMatch[fsMatch].result == NULL) {
    if (! pdfxError(ixc, PDFXERR_FS_MISSING))
      return FALSE;
  }
  else {
    if (oNameNumber(*urlMatch[fsMatch].result) != NAME_URL) {
      if (! pdfxError(ixc, PDFXERR_FS_NOT_URL))
        return FALSE;
    }
  }

  if (urlMatch[fMatch].result == NULL) {
    if (! pdfxError(ixc, PDFXERR_F_MISSING))
      return FALSE;
  }

  if (urlMatch[efMatch].result != NULL ||
      urlMatch[dosMatch].result != NULL ||
      urlMatch[macMatch].result != NULL ||
      urlMatch[unixMatch].result != NULL) {
    if (! pdfxError(ixc, PDFXERR_EXTRA_KEYS_IN_URL))
      return FALSE;
  }

  return TRUE;
}

/**
 * Check that the ID strings in the pass refId match those in the ID in the
 * external document trailer.
 *
 * \param reportJobName Set to TRUE if the name of the target file should be
 *        shown.
 * \param versionMismatch Set to TRUE if the target document has the expected
 *        ID, but is a different version.
 * \return FALSE on error.
 */
static Bool checkTrailerIdsMatch(PDFCONTEXT* pdfc,
                                 OBJECT* refId,
                                 OBJECT* externalDocTrailer,
                                 Bool* reportJobName,
                                 Bool* versionMismatch)
{
  NAMETYPEMATCH trailerMatch[] = {
    { NAME_ID | OOPTIONAL, 2, { OARRAY, OINDIRECT }},
    DUMMY_END_MATCH };
  OBJECT* result;
  GET_PDFXC_AND_IXC;

  *reportJobName = TRUE;
  *versionMismatch = FALSE;

  /* Validate the reference ID. */
  if (refId == NULL) {
    return pdfxError(ixc, PDFXERR_MISSING_REF_ID);
  }

  HQASSERT(oType(*refId) == OARRAY, "Expected array.");

  if (theLen(*refId) != 2 ||
      oType(oArray(*refId)[0]) != OSTRING ||
      oType(oArray(*refId)[1]) != OSTRING) {
    return pdfxError(ixc, PDFXERR_TRAILER_ID_MALFORMED);
  }

  if (! pdf_dictmatch(pdfc, externalDocTrailer, trailerMatch))
    return FALSE;

  /* Validate the target document's ID and compare to the reference's. */
  result = trailerMatch[0].result;
  if (result == NULL) {
    return pdfxError(ixc, PDFXERR_CANNOT_VERIFY_EXTERNAL_FILE_ID);
  }
  else {
    if (theLen(*result) != 2 ||
        oType(oArray(*result)[0]) != OSTRING ||
        oType(oArray(*result)[1]) != OSTRING) {
      return pdfxError(ixc, PDFXERR_TRAILER_ID_MALFORMED);
    }

    if (compare_objects(refId, result)) {
      *reportJobName = FALSE;
    }
    else {
      /* Full ID's are different; check for just a different version. */
      if (compare_objects(&oArray(*refId)[0], &oArray(*result)[0])) {
        *versionMismatch = TRUE;
      }
      else {
        return pdfxError(ixc, PDFXERR_EXTERNAL_FILE_IDS_DIFFER);
      }
    }
  }
  return TRUE;
}

/**
 * Compare the passed metadata version objects. Documents are identified
 * primarily by the document ID; version information is provided by the document
 * version and rendition class objects.
 *
 * \param versionMismatch Set to TRUE only if the document ID's match but the
 *        other information differs; otherwise the pointed-to value will not be
 *        changed.
 * \return TRUE is the passed ID's match.
 */
static Bool compareMetadataVersions(OBJECT* id1, OBJECT* id2,
                                    OBJECT* version1, OBJECT* version2,
                                    OBJECT* renditionClass1,
                                    OBJECT* renditionClass2,
                                    Bool* versionMismatch) {
  if (compare_objects(id1, id2)) {
    /* Id's match; check version. */
    if (! (((version1 == NULL && version2 == NULL) ||
           compare_objects(version1, version2)) &&
          ((renditionClass1 == NULL && renditionClass2 == NULL) ||
           compare_objects(renditionClass1, renditionClass2)))) {
      *versionMismatch = TRUE;
    }
    return TRUE;
  }
  return FALSE;
}

/**
 * Check that document ID information in the passed reference xObject metadata
 * and external document metadata match.
 *
 * \param reportJobName Set to TRUE if the name of the target file should be
 *        shown.
 * \param versionMismatch Set to TRUE if the target document has the expected
 *        ID, but is a different version.
 * \return FALSE on error.
 */
Bool checkMetadataIdsMatch(PDFCONTEXT* pdfc,
                           OBJECT* xObjectMetadata,
                           DocumentMetadata* externalDocMetadata,
                           Bool* reportJobName,
                           Bool* versionMismatch)
{
  /* Note that in the XMP ResourceRef we check for the PDF/X doc'd
   * metadata (which is basically a bit wrong), and the correct XMP
   * doc'd metadata. */
  enum {
    xmpMM_dId = 0, xmpMM_vId = 1, xmpMM_rC = 2,
    stRef_dId = 3, stRef_vId = 4, stRef_rC = 5,
    stRef_mis_dId = 6, stRef_mis_vId = 7, stRef_mis_rC = 8};

  NAMETYPEMATCH match[] = {
    {NAME_xmpMM_DocumentID | OOPTIONAL, 1, {OSTRING}}, /* Prefix was wrong in PDF/X-5:2008 final. */
    {NAME_xmpMM_VersionID | OOPTIONAL, 1, {OSTRING}},
    {NAME_xmpMM_RenditionClass | OOPTIONAL, 1, {OSTRING}},
    {NAME_stRef_documentID | OOPTIONAL, 1, {OSTRING}}, /* Intended correct versions as per XMP spec. */
    {NAME_stRef_versionID | OOPTIONAL, 1, {OSTRING}},
    {NAME_stRef_renditionClass | OOPTIONAL, 1, {OSTRING}},
    {NAME_stRef_DocumentID | OOPTIONAL, 1, {OSTRING}}, /* PDF/X-5:2010 final mis-spellings. */
    {NAME_stRef_VersionID | OOPTIONAL, 1, {OSTRING}},
    {NAME_stRef_RenditionClass | OOPTIONAL, 1, {OSTRING}},
    DUMMY_END_MATCH };

  OBJECT metadataDictionary = OBJECT_NOTVM_NULL;
  Bool success = TRUE;

  GET_PDFXC_AND_IXC;

  *reportJobName = TRUE;
  *versionMismatch = FALSE;

  /* Validate the xObject metadata. */
  if (xObjectMetadata == NULL)
    return pdfxError(ixc, PDFXERR_MISSING_REF_METADATA);

  if (oType(externalDocMetadata->id) != OSTRING)
    return pdfxError(ixc, PDFXERR_CANNOT_VERIFY_EXTERNAL_FILE_METADATA);

  if (! pdfMetadataParse(pdfc, oFile(*xObjectMetadata), &metadataDictionary))
    return FALSE;

  /* From here on we must free metadataDictionary before returning. */

  if (! pdf_dictmatch(pdfc, &metadataDictionary, match)) {
    success = FALSE ;
  } else {
    OBJECT *id = NULL, *version = NULL;

    if (match[xmpMM_dId].result != NULL) {
      /* The reference is using the xmpMM prefix. Which is the wrong
         prefix. */
      id = match[xmpMM_dId].result ;
      version = match[xmpMM_vId].result ;

    } else if (match[stRef_dId].result != NULL) {
      /* The reference is using the stRef prefix. Which is
         correct! */
      id = match[stRef_dId].result ;
      version = match[stRef_vId].result ;

    } else if (match[stRef_mis_dId].result != NULL) {
      /* The reference is using the stRef prefix but the wrong
         spelling (but this was in PDF/X-5:2010 final). */
      id = match[stRef_mis_dId].result ;
      version = match[stRef_mis_vId].result ;

    } else {
      /* Neither type of doc ID was found. */
      success = pdfxError(ixc, PDFXERR_MISSING_REF_METADATA);
    }

    /* We had one of the above matches so compare the versions. */
    if (success && compareMetadataVersions(&externalDocMetadata->id, id,
                                           &externalDocMetadata->version, version,
                                           &externalDocMetadata->renditionClass,
                                           match[xmpMM_rC].result,
                                           versionMismatch)) {
      *reportJobName = *versionMismatch;
    } else {
      (void)pdfxError(ixc, PDFXERR_EXTERNAL_FILE_METADATA_DIFFER);
    }
  }

  pdf_freeobject(pdfc, &metadataDictionary);

  return success;
}

/* See header for doc. */
Bool pdfxCheckExternalFileId(PDFCONTEXT* pdfc,
                             FILELIST* externalFile,
                             OBJECT* xObjectMetadata,
                             DocumentMetadata* externalDocMetadata,
                             OBJECT* refId,
                             OBJECT* externalDocTrailer)
{
  Bool success = TRUE;
  Bool idReportJobName, metadataReportJobName;
  Bool idVersionMismatch, metadataVersionMismatch;
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  /* Note that we run both checkers to report ID and Metadata mismatches. */
  success = checkTrailerIdsMatch(pdfc, refId, externalDocTrailer,
                                 &idReportJobName, &idVersionMismatch);
  success = checkMetadataIdsMatch(pdfc, xObjectMetadata, externalDocMetadata,
                                  &metadataReportJobName,
                                  &metadataVersionMismatch) && success;

  if (success) {
    if (idVersionMismatch || metadataVersionMismatch) {
      /* Only the version is different. This is basically ok (we don't
       * want to raise an error), but we'll report the difference. */

      /* UVM("****** %s External file is a different version from that expected") */
      pdfxPrint(ixc, (uint8*)"External file is a different version from that expected\n");
    }

    if (idReportJobName || metadataReportJobName) {
      /* UVM("****** %s External file: %s") */
      pdfxPrint(ixc, (uint8*)"External file: ");
      monitorf((uint8*)"%.*s\n", externalFile->len, externalFile->clist);
    }
  }

  return success;
}

/* Log stripped */
