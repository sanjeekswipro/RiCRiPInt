/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfx.c(EBDSDK_P.1) $
 * $Id: src:pdfx.c,v 1.49.2.1.1.1 2013/12/19 11:25:13 anon Exp $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file is the main entrypoint for PDF/X conformance testing.
 */

#include "core.h"
#include "pdfx.h"
#include "pdfxPrivate.h"

#include "pdfcolor.h"           /* NULL_COLORSPACE */
#include "pdfdefs.h"            /* PDF_ACCEPT_ANY */
#include "pdfin.h"              /* PDF_IXC_PARAMS */
#include "pdfmatch.h"           /* pdf_dictmatch */
#include "pdfmem.h"             /* pdf_freeobject */

#include "gstack.h"             /* gstateptr */
#include "gschcms.h"            /* gsc_setrenderingintent */
#include "hqmemcmp.h"           /* HqMemCmp */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "swerrors.h"           /* TYPECHECK */

/* --Utility Functions-- */

/* Strings that identify particular PDF/X source file versions; these are the
values for the GTS_PDFXVersion key in the info dictionary. Although we do not
support PDF/X-1:2001, the same identifier is also used for PDF/X-1a:2001 files
(discrimination between these two is provided by the GTS_PDFXConformance key),
which we do support. */
static struct pdf_x_header_types {
  char* string;
  uint32 version;
} pdfxHeaderVersions[] = {
  {"PDF/X-1:2001", PDF_ENFORCE_X1a_2001}, /* See PDF/X1 vs X1-a note above. */
  {"PDF/X-1a:2003", PDF_ENFORCE_X1a_2003},
  {"PDF/X-3:2002", PDF_ENFORCE_X3_2002},
  {"PDF/X-3:2003", PDF_ENFORCE_X3_2003},
  {"PDF/X-4", PDF_ENFORCE_X4},
  {"PDF/X-4p", PDF_ENFORCE_X4p},
  {"PDF/X-5g", PDF_ENFORCE_X5g},
  {"PDF/X-5n", PDF_ENFORCE_X5n},
  {"PDF/X-5pg", PDF_ENFORCE_X5pg},
};
#define N_PDF_X_HEADER_TYPES \
  (sizeof(pdfxHeaderVersions) / sizeof(pdfxHeaderVersions[0]))

/** Return the PDF/X version for the passed string.
*/
static uint32 getVersionFromString(OBJECT* string)
{
  int32 i;
  uint8 *s = oString(*string);
  int32 length = theLen(*string);

  for (i = 0; i < N_PDF_X_HEADER_TYPES; i++) {
    if (HqMemCmp(s, length, (uint8*)pdfxHeaderVersions[i].string,
                 strlen_int32(pdfxHeaderVersions[i].string)) == 0)
      break;
  }

  if (i == N_PDF_X_HEADER_TYPES)
    return PDF_ENFORCE_UNKNOWN;
  else
    return pdfxHeaderVersions[i].version;
}

/** Return a version string for the passed version number, which should be one
of PDF_ENFORCE_X1a_2001, PDF_ENFORCE_X1a_2003, etc.

Returns NULL if no matching version is found.
*/
static char* getStringFromVersion(uint32 version)
{
  /* Special case for X-1a:2001, since the detection string is different from
  the reporting string (see comment above pdf_x_header_types structure). */
  if (version == PDF_ENFORCE_X1a_2001)
    return "PDF/X-1a:2001";
  else {
    int32 i;
    for (i = 0; i < N_PDF_X_HEADER_TYPES; i++) {
      if (pdfxHeaderVersions[i].version == version)
        return pdfxHeaderVersions[i].string;
    }
    return NULL;
  }
}

/**
 * Most PDF/X violations only need to be printed once per job, e.g. "Something
 * not allowed"; however some violations should be printed for every occurence,
 * e.g. "External file invalid".
 *
 * \return TRUE if all occurences of the passed error should be reported.
 */
static Bool neverTreatAsDuplicate(int32 error)
{
  switch (error) {
    default:
      return FALSE;

    case PDFXERR_TRAILER_ID_MALFORMED:
    case PDFXERR_MISSING_REF_ID:
    case PDFXERR_CANNOT_VERIFY_EXTERNAL_FILE_ID:
    case PDFXERR_EXTERNAL_FILE_IDS_DIFFER:
    case PDFXERR_MISSING_REF_METADATA:
    case PDFXERR_CANNOT_VERIFY_EXTERNAL_FILE_METADATA:
    case PDFXERR_EXTERNAL_FILE_METADATA_DIFFER:
      return TRUE;
  }
}

/* See header for doc. */
void pdfxPrint(PDF_IXC_PARAMS *ixc, uint8* message)
{
  char* versionString = NULL;

  /* Which version of PDF/X are we reporting for? */
  versionString = getStringFromVersion(ixc->conformance_pdf_version);

  /* Note that the PDF/X version may not actually be known yet. */
  if (versionString == NULL)
    versionString = "PDF/X";

  monitorf((uint8*)("****** %s %s"), versionString, message);
}

/* For PDF/X checks, reports the given error. Once an error has been reported
it is impeded from being reported again.
*/
Bool pdfxError(PDF_IXC_PARAMS *ixc, int32 error)
{
  HQASSERT(ixc != NULL, "ixc is NULL");
  HQASSERT(error >= 0 && error < NUM_PDFXERRS, "pdfxError - value out of range") ;

  /* Only print each error once. */
  if (! ixc->enforce_pdf_warnings[error]) {
    if (ixc->suppress_duplicate_warnings && ! neverTreatAsDuplicate(error)) {
      ixc->enforce_pdf_warnings[error] = TRUE ;
    }

    /* Reset PDFXVersionValid (for the sake of the pdfxstatus operator) only
       if it has been set as a boolean.  It stays null if the file was never
       properly identified as a PDF/X file. */
    if (oType(ixc->PDFXVersionValid) == OBOOLEAN &&
        error != PDFXERR_OVERPRINT_OVERRIDDEN)
      oBool(ixc->PDFXVersionValid) = FALSE;

    /* Print appropriate severity - ie. error or warning */
    /* UVM("****** %s Error: %t") */
    /* UVM("****** %s Warning: %t") */
    if (ixc->abort_for_invalid_types && error != PDFXERR_OVERPRINT_OVERRIDDEN)
      pdfxPrint(ixc, (uint8*)"Error: ") ;
    else
      pdfxPrint(ixc, (uint8*)"Warning: ");

    switch (error) {
      case PDFXERR_VERSION_UNKNOWN:
        monitorf(UVS("GTS_PDFXVersion string is unrecognised"));
        break;
      case PDFXERR_VERSION_INCORRECT:
        monitorf(UVS("GTS_PDFXVersion is incorrect"));
        break;
      case PDFXERR_INFO_KEY_MISSING:
        monitorf(UVS("Info key missing"));
        break;
      case PDFXERR_NO_PDFX_VERSION:
        monitorf(UVS("No GTS_PDFXVersion"));
        break;
      case PDFXERR_NO_CONFORMANCE_KEY:
        monitorf(UVS("No GTS_PDFXConformance key"));
        break;
      case PDFXERR_UNWANTED_CONF_KEY:
        monitorf(UVS("GTS_PDFXConformance key present but not required"));
        break;
      case PDFXERR_BAD_CONF_KEY:
        monitorf(UVS("GTS_PDFXConformance key has unrecognised value"));
        break;
      case PDFXERR_X1_NOT_SUPPORTED:
        monitorf(UVS("PDF/X-1:2001 is not supported"));
        break;
      case PDFXERR_VERSION_NOT_SUPPORTED:
        monitorf(UVS("PDF/X version not supported"));
        break;
      case PDFXERR_TITLE_KEY_MISSING:
        monitorf(UVS("Title key is missing"));
        break;
      case PDFXERR_CREATION_DATE_MISSING:
        monitorf(UVS("Creation date is missing"));
        break;
      case PDFXERR_MODIFICATION_DATE_MISSING:
        monitorf(UVS("Modification date is missing"));
        break;
      case PDFXERR_NO_TRAPPED_KEY:
        monitorf(UVS("No Trapped key"));
        break;
      case PDFXERR_TRAPPED_UNKNOWN:
        monitorf(UVS("Trapped key is not /True or /False"));
        break;
      case PDFXERR_TRAILER_ID:
        monitorf(UVS("ID key missing"));
        break;
      case PDFXERR_ENCRYPT_KEY:
        monitorf(UVS("Encrypt key not allowed"));
        break;
      case PDFXERR_INVALID_rg_OPERATOR:
        monitorf(UVS("Content stream contains \"rg\" operator"));
        break;
      case PDFXERR_INVALID_RG_OPERATOR:
        monitorf(UVS("Content stream contains \"RG\" operator"));
        break;
      case PDFXERR_INVALID_PS_OPERATOR:
        monitorf(UVS("Content stream contains \"PS\" operator"));
        break;
      case PDFXERR_INVALID_ri_OPERATOR:
        monitorf(UVS("Content stream contains \"ri\" operator"));
        break;
      case PDFXERR_INVALID_OPERATOR:
        monitorf(UVS("Content stream contains invalid operator"));
        break;
      case PDFXERR_HALFTONE_TYPE:
        monitorf(UVS("Invalid halftone type"));
        break;
      case PDFXERR_HTP:
        monitorf(UVS("Has HTP ExtGState key"));
        break;
      case PDFXERR_TR:
        monitorf(UVS("Has TR ExtGState key"));
        break;
      case PDFXERR_TR2:
        monitorf(UVS("Has TR2 ExtGState key"));
        break;
      case PDFXERR_TR2_INVALID:
        monitorf(UVS("TR2 ExtGState key must have a value of /Default, if "
                     "present"));
        break;
      case PDFXERR_HALFTONENAME_KEY:
        monitorf(UVS("Has HalftoneName key in Halftone dictionary"));
        break;
      case PDFXERR_FILE_COMPRESSION:
        monitorf(UVS("Stream uses invalid filter (compression)"));
        break;
      case PDFXERR_NO_FONTDESCRIPTOR:  /* Should read "Font of type %s..." */
        monitorf(UVS("Font is missing FontDescriptor key"));
        break;
      case PDFXERR_NO_FONTFILE:        /* Should read "Font of type %s..." */
        monitorf(UVS("Font is missing a FontFile within the FontDescriptor"));
        break;
      case PDFXERR_OPI_KEY:
        monitorf(UVS("Image or Form XObject has OPI key"));
        break;
      case PDFXERR_FILESPEC:
        monitorf(UVS("File specifications are not permitted"));
        break;
      case PDFXERR_FILESPEC_NOT_EMBEDDED:
        monitorf(UVS("File specification refers to an external file"));
        break;
      case PDFXERR_F_KEY_PROHIBITED:
        monitorf(UVS("Stream dictionary contains the 'F' key"));
        break;

      case PDFXERR_NO_MEDIABOX:
        monitorf(UVS("No MediaBox"));
        break;
      case PDFXERR_ARTANDTRIM:
        monitorf(UVS("ArtBox and TrimBox are defined on the page"));
        break;
      case PDFXERR_NO_ARTORTRIM:
        monitorf(UVS("No ArtBox or TrimBox are defined on the page"));
        break;
      case PDFXERR_TA_BOX_NOTIN_BLEEDBOX:
        monitorf(UVS("BleedBox does not contain the TrimBox/ArtBox"));
        break;
      case PDFXERR_BTA_BOX_NOTIN_CROPBOX:
        monitorf(UVS("CropBox does not contain the BleedBox/TrimBox/ArtBox"));
        break;
      case PDFXERR_CBTA_BOX_NOTIN_MEDIABOX:
        monitorf(UVS("MediaBox does not contain the CropBox/BleedBox/TrimBox"
                     "/ArtBox"));
        break;
      case PDFXERR_ANNOT_ON_PAGE:
        monitorf(UVS("Annotation within bounding box of page"));
        break;
      case PDFXERR_PRINTERMARK_ANNOT_ON_PAGE:
        monitorf(UVS("PrinterMark annotation within TrimBox/ArtBox"));
        break;
      case PDFXERR_OVERPRINT_OVERRIDDEN:
        monitorf(UVS("Overprint options overridden; reverting to defaults"));
        break;
      case PDFXERR_PS_XOBJECT:
        monitorf(UVS("Has PostScript Xobject"));
        break;
      case PDFXERR_INFO_TRAPPED_NOT_TRUE:
        monitorf(UVS("Trapped key in Info dictionary is not True despite "
                     "TrapNet annotation"));
        break;
      case PDFXERR_TRAPNET_FONTFAUXING:
        monitorf(UVS("TrapNet annotation FontFauxing present"));
        break;
      case PDFXERR_ALTERNATE_FOR_PRINTING:
        monitorf(UVS("Image XObject alternate default for printing"));
        break;
      case PDFXERR_ACTIONS:
        monitorf(UVS("Actions are not allowed"));
        break;
      case PDFXERR_NO_OUTPUTINTENT:
        monitorf(UVS("OutputIntent is missing from the file"));
        break;
      case PDFXERR_MULTIPLE_GTS_PDFX:
        monitorf(UVS("Multiple GTS_PDFX OutputIntent dictionaries"));
        break;
      case PDFXERR_NO_OUTPUT_CONDITION:
        monitorf(UVS("OutputIntent is missing the OutputCondition key"));
        break;
      case PDFXERR_NO_DEST_OUTPUT_PROFILE:
        monitorf(UVS("OutputIntent is missing the DestOutputProfile key"));
        break;
      case PDFXERR_BAD_REGNAME:
        monitorf(UVS("RegistryName in OutputIntent is missing or invalid"));
        break;
      case PDFXERR_NO_OI_INFO_KEY:
        monitorf(UVS("Info key in OutputIntent is missing"));
        break;
      case PDFXERR_NO_PROFILE_FILE:
        monitorf(UVS("No OutputIntent color management: registered ICC profile "
                     "listed but not found"));
        break;
      case PDFXERR_NO_REG_PROFILE:
        monitorf(UVS("No OutputIntent color management: no registered ICC "
                     "profile listed"));
        break;

      case PDFXERR_BAD_X1a_COLORSPACE:   /* X-1a response only */
        monitorf(UVS("Colorspace must be one of DeviceCMYK, DeviceGray, "
                     "Separation, DeviceN, Indexed or Pattern."));
        break;
      case PDFXERR_BAD_X3_COLORSPACE:   /* X-3 response only */
        monitorf(UVS("Colorspace must be one of DeviceCMYK, DeviceGray, "
                     "DeviceRGB, Separation, DeviceN, Indexed or Pattern."));
        break;
      case PDFXERR_NOT_VALID_DEFAULT_SPACE:
        monitorf(UVS("Default color space is not one of ICCBased, CalGray, or "
                     "CalRGB."));
        break;
      case PDFXERR_BASE_COLORSPACE:
        monitorf(UVS("Invalid base color space to Indexed or Pattern color "
                     "space"));
        break;
      case PDFXERR_ALTERNATIVE_COLORSPACE:
        monitorf(UVS("Invalid alternative to Separation or DeviceN color space"));
        break;

      case PDFXERR_UNDEFINED_RESOURCE_IN_OI:
        monitorf(UVS("The OutputIntent dictionary refers to an undefined "
                     "resource"));
        break;
      case PDFXERR_BAD_OI_DICT_TYPE:
        monitorf(UVS("The OutputIntent dictionary should have a value of "
                     "'OutputIntent' for the Type key"));
        break;

      case PDFXERR_PRESEP:
        monitorf(UVS("Separations for each page described as separate page "
                     "objects"));
        break;
      case PDFXERR_INVALID_PERMISSIONS_DICT:
        monitorf(UVS("Permissions dictionary can only contain keys for UR and "
                     "UR3"));
        break;
      case PDFXERR_INVALID_VIEWER_PREFS_DICT:
        monitorf(UVS("Invalid ViewArea, ViewClip, PrintArea or PrintClip value "
                     "in ViewerPreferences dictionary"));
        break;
      case PDFXERR_VIEWER_PREFS_BLEEDBOX_MISSING:
        monitorf(UVS("BleedBox specified in ViewPreferences dictionary, but "
                     "page has no BleedBox"));
        break;
      case PDFXERR_ALTERNATE_PRESENTATIONS:
        monitorf(UVS("AlternatePresentations dictionary is not permitted"));
        break;
      case PDFXERR_PRESSTEPS:
        monitorf(UVS("PresSteps dictionary is not permitted"));
        break;
      case PDFXERR_INVALID_INTENT:
        monitorf(UVS("Invalid rendering intent"));
        break;
      case PDFXERR_XFA_KEY:
        monitorf(UVS("XFA key in an AcroForm dictionary is not permitted"));
        break;
      case PDFXERR_INVALID_OUTPUTCONDITION_DEVICE_CS:
        monitorf(UVS("Output Condition device space invalid"));
        break;

      case PDFXERR_OC_NAME_MISSING:
        monitorf(UVS("Name key missing in optional content configuration "
                     "dictionary"));
        break;
      case PDFXERR_OC_AS_INVALID:
        monitorf(UVS("AS key should not be present in optional content "
                     "configuration dictionary"));
        break;

      case PDFXERR_REFERENCEXOBJECT:
        monitorf(UVS("Reference XObjects are not permitted"));
        break;
      case PDFXERR_PRINTERMARKS_NOT_IN_1_3:
        monitorf(UVS("PrinterMark annotations are a feature of PDF 1.4"));
        break;
      case PDFXERR_1_3_BASED_USING_1_4_GSTATE:
        monitorf(UVS("PDF 1.3 Based PDF/X cannot access transparency members "
                     "of the gstate"));
        break;

      case PDFXERR_INVALID_CONSTANT_ALPHA:
        monitorf(UVS("Constant Alpha can only be set to 1.0"));
        break;
      case PDFXERR_INVALID_SOFT_MASK:
        monitorf(UVS("Only 'None' is allowed for a SoftMask"));
        break;
      case PDFXERR_INVALID_BLEND_MODE:
        monitorf(UVS("Only 'Normal' and 'Compatible' blend modes are allowed"));
        break;
      case PDFXERR_TRANSPARENCY_GROUP:
        monitorf(UVS("Transparency groups are not permitted"));
        break;
      case PDFXERR_SOFT_MASKED_IMAGE:
        monitorf(UVS("Soft-masked images are not permitted"));
        break;

      case PDFXERR_16_BIT_IMAGE:
        monitorf(UVS("16 bit images are not permitted"));
        break;
      case PDFXERR_OBJECT_STREAM:
        monitorf(UVS("Object streams are not permitted"));
        break;
      case PDFXERR_XREF_STREAM:
        monitorf(UVS("Cross-reference streams are not permitted"));
        break;
      case PDFXERR_OPTIONAL_CONTENT:
        monitorf(UVS("Optional content is not permitted"));
        break;

      case PDFXERR_URLS_MISSING:
        monitorf(UVS("URLs key missing from DestOutputProfileRef dictionary"));
        break;

      case PDFXERR_PROFILE_REF_KEYS_MISSING:
        monitorf(UVS("Required key missing from DestOutputProfileRef dictionary"));
        break;

      case PDFXERR_PROFILE_REF_CONTAINS_COLORANT_TABLE:
        monitorf(UVS("DestOutputProfileRef dictionary should not contain the "
                     "ColorantTable key"));
        break;

      case PDFXERR_FS_MISSING:
        monitorf(UVS("FS key missing from URL dictionary"));
        break;

      case PDFXERR_FS_NOT_URL:
        monitorf(UVS("FS is not 'URL' in URL dictionary"));
        break;

      case PDFXERR_F_MISSING:
        monitorf(UVS("F key missing from URL dictionary"));
        break;

      case PDFXERR_EXTRA_KEYS_IN_URL:
        monitorf(UVS("URL dictionary should not contain extra keys"));
        break;

      case PDFXERR_EXTERNAL_PROFILE_FORBIDDEN:
        monitorf(UVS("External output profile not allowed"));
        break;

      case PDFXERR_INTERNAL_AND_EXTERNAL_PROFILES:
        monitorf(UVS("Both internal and external output profiles specified"));
        break;

      case PDFXERR_EXTERNAL_PROFILE_NON_DICTIONARY_URL:
        monitorf(UVS("URL for external profile is not file specification dictionary"));
        break;

      case PDFXERR_EXTERNAL_PROFILE_INVALID_CHECKSUM:
        monitorf(UVS("External profile reference checksum is invalid"));
        break;

      case PDFXERR_EXTERNAL_PROFILE_CHECKSUM_DIFFERS:
        monitorf(UVS("External profile checksum differs from that expected"));
        break;

      case PDFXERR_TRAILER_ID_MALFORMED:
        monitorf(UVS("Document ID malformed; expected array of two strings"));
        break;

      case PDFXERR_MISSING_REF_ID:
        monitorf(UVS("Reference to external file does not provide document ID"));
        break;

      case PDFXERR_CANNOT_VERIFY_EXTERNAL_FILE_ID:
        monitorf(UVS("Cannot verify that external PDF file is correct as the "
                     "ID entry is missing from the document trailer"));
        break;

      case PDFXERR_EXTERNAL_FILE_IDS_DIFFER:
        monitorf(UVS("External PDF file trailer ID differs from that expected"));
        break;

      case PDFXERR_MISSING_REF_METADATA:
        monitorf(UVS("Reference to external file does not provide identifying "
                     "metadata"));
        break;

      case PDFXERR_CANNOT_VERIFY_EXTERNAL_FILE_METADATA:
        monitorf(UVS("Cannot verify that external PDF file is correct as "
                     "identifying metadata is missing"));
        break;

      case PDFXERR_EXTERNAL_FILE_METADATA_DIFFER:
        monitorf(UVS("External PDF file DocumentID differs from that expected"));
        break;

      default:
        HQFAIL("Unhandled PDF/X error condition");
        break;
    }

    monitorf((uint8 *)"\n");
  }

  if (ixc->abort_for_invalid_types  && error != PDFXERR_OVERPRINT_OVERRIDDEN)
    return error_handler(SYNTAXERROR);

  return TRUE;
}


/* --Query Methods-- */

/* See header for doc. */
Bool pdfxExternalFilesAllowed(PDFCONTEXT* pdfc)
{
  GET_PDFXC_AND_IXC;

  return (ixc->conformance_pdf_version &
          (PDF_CHECK_EXTERNAL_GRAPHICS_ALLOWED |
           PDF_CHECK_EXTERNAL_PROFILE_ALLOWED)) != 0;
}

/** Set the PDF/X version claimed in the execution context to the passed string.
*/
static Bool setVersionClaimed(PDFCONTEXT* pdfc, uint8* string, uint32 length)
{
  GET_PDFXC_AND_IXC;

  /* Free any existing string. */
  if (oType(ixc->PDFXVersionClaimed) == OSTRING)
    pdf_freeobject(pdfc, &ixc->PDFXVersionClaimed);

  if (! pdf_create_string(pdfc, length, &ixc->PDFXVersionClaimed))
    return FALSE;

  HqMemCpy(oString(ixc->PDFXVersionClaimed), string, length);
  return TRUE;
}

/** Determin the PDF/X version from the passed objects, all of which must be
null or string objects.

The PDF/X version can be specified in two places depending on the version; prior
to PDF/X-4, the version is always specified in the info dictionary. PDF/X-4 and
later versions specify the version in the document metadata.
*/
static Bool determinPDFXVersion(PDFCONTEXT *pdfc,
                                OBJECT *versionFromInfoDict,
                                OBJECT *conformanceFromInfoDict,
                                OBJECT *versionFromMetadata)
{
  int32 foundVersion;
  OBJECT* version;
  Bool report_fault = FALSE;
  GET_PDFXC_AND_IXC;

  /* Choose the metadata version in preference to the info dictionary version. */
  if (versionFromMetadata != NULL)
    version = versionFromMetadata;
  else
    version = versionFromInfoDict;

  if (version == NULL) {
    /* No PDF/X version has been provided. */
    if (ixc->EnforcePDFVersion != PDF_ACCEPT_ANY &&
        ixc->EnforcePDFVersion != PDF_ACCEPT_AUTO_DETECT) {
      if (! pdfxError(ixc, PDFXERR_NO_PDFX_VERSION))
        return FALSE;
    }
    return TRUE;
  }

  if (! setVersionClaimed(pdfc, oString(*version), theLen(*version)))
    return FALSE;
  foundVersion = getVersionFromString(version);

  if (foundVersion == PDF_ENFORCE_UNKNOWN) {
    /* Show an error if we are enforcing a particular version or
    autodetecting. */
    if (ixc->EnforcePDFVersion != PDF_ACCEPT_ANY) {
      if (! pdfxError(ixc, PDFXERR_VERSION_UNKNOWN))
        return FALSE;
    }
    return TRUE;
  }

  ixc->conformance_pdf_version = foundVersion;

  /* Is the document's version allowed? */
  switch (ixc->EnforcePDFVersion) {
    case PDF_ACCEPT_AUTO_DETECT:
      break;

    case PDF_ACCEPT_ANY:
      ixc->conformance_pdf_version = 0;
      break;

    case PDF_ACCEPT_X1a:
      if (ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2001 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2003)
        report_fault = TRUE;
        break;

    case PDF_ACCEPT_X3_X1a:
      if (ixc->conformance_pdf_version != PDF_ENFORCE_X3_2002 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X3_2003 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2001 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2003)
        report_fault = TRUE;
        break;

    case PDF_ACCEPT_X4_X3_X1a:
      if (ixc->conformance_pdf_version != PDF_ENFORCE_X4 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X3_2002 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X3_2003 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2001 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2003)
        report_fault = TRUE;
        break;

    case PDF_ACCEPT_X4p_X4_X3_X1a:
      if (ixc->conformance_pdf_version != PDF_ENFORCE_X4p &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X4 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X3_2002 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X3_2003 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2001 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2003)
        report_fault = TRUE;
        break;

    case PDF_ACCEPT_X5g_X5pg_X4p_X3_X3_X1a:
      if (ixc->conformance_pdf_version != PDF_ENFORCE_X5g &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X5pg &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X4p &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X4 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X3_2002 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X3_2003 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2001 &&
          ixc->conformance_pdf_version != PDF_ENFORCE_X1a_2003)
        report_fault = TRUE;
        break;

    default:
      /* The above enumeration of PDF_ACCEPT_* #defines should be complete. */
      HQFAIL("determinPDFXVersion - Unrecognised PDF version user request.");
      report_fault = TRUE;
      break;
  }

  if (report_fault) {
    ixc->conformance_pdf_version = 0;
    if (! pdfxError(ixc, PDFXERR_VERSION_INCORRECT))
      return FALSE;
  }

  /* Return if not enforcing PDF/X. */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  /* The /GTS_PDFXConformance key may or may not be present. Note that the
  difference between X-1:2001 and X-1a:2001 can be determined only by the
  presence of this key.  So if we've seen X-1:2001 in the GTS_PDFXVersion
  key, then 'EnforcePDFVersion' will already be set to "x1a" (as we don't
  "support" X1-not-a!, 2001). */
  if (conformanceFromInfoDict) {
    /* The conformance key is only valid for X1:2001 variants. */
    if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_2001_ONLY) == 0) {
      if (! pdfxError(ixc, PDFXERR_UNWANTED_CONF_KEY))
        return FALSE;
    }
    else {
      char *v1 = "PDF/X-1:2001";
      char *v1a = "PDF/X-1a:2001";
      OBJECT *c = conformanceFromInfoDict;
      /* If the value is not one of "PDF/X-1:2001" or "PDF/X-1a:2001", throw an
      error. */
      if (HqMemCmp(oString(*c), theLen(*c), (uint8*)v1, strlen_int32(v1)) != 0 &&
          HqMemCmp(oString(*c), theLen(*c), (uint8*)v1a, strlen_int32(v1a)) != 0) {
        if (! pdfxError(ixc, PDFXERR_BAD_CONF_KEY))
          return FALSE;
      }
      else {
        /* We don't support X1. */
        if (HqMemCmp(oString(*c), theLen(*c), (uint8*)v1, strlen_int32(v1)) == 0) {
          /* Revert to no enforcement. */
          ixc->conformance_pdf_version = 0;
          if (! pdfxError(ixc, PDFXERR_X1_NOT_SUPPORTED))
            return FALSE;
        }
        else {
          /* The conformance is PDF/X-1a:2001 - this is the only way that this
          version is identified (as the 'version' string just says
          'x-1:2001') - update the version claimed. */
          HQASSERT(HqMemCmp(oString(*c), theLen(*c), (uint8*)v1a,
                            strlen_int32(v1a)) == 0,
                   "determinPDFXVersion - 'conformance' should be PDF/X-1a:2001");
          if (! setVersionClaimed(pdfc, oString(*conformanceFromInfoDict),
                                  theLen(*conformanceFromInfoDict)))
            return FALSE;
        }
      }
    }
  }
  else {
    /* The conformance key should be present for X1:2001 variants. */
    if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_2001_ONLY) != 0)
      if (! pdfxError(ixc, PDFXERR_NO_CONFORMANCE_KEY))
        return FALSE;
  }

  return TRUE;
}


/** Report the PDF version in effect.
*/
static void reportPDFVersion(PDF_IXC_PARAMS *ixc)
{
  OCopy(ixc->PDFXVersionTreated, fnewobj) ;

  if (ixc->conformance_pdf_version == 0) {
    monitorf((uint8*)"PDF version %d.%d\n", ixc->majorversion,
             ixc->minorversion);
  }
  else {
    char *pTxt = getStringFromVersion(ixc->conformance_pdf_version);
    if (pTxt == NULL) {
      HQFAIL("reportPDFVersion - Unknown value of ixc->conformance_pdf_version");
      pTxt = "Unknown";
    }
    else {
      OCopy(ixc->PDFXVersionTreated, tnewobj);
    }
    monitorf((uint8*)"PDF/X version %s\n", pTxt);
  }
}

/** Conformance Level: All

This method determins the PDF/X enforcement level from the passed document Info
and document metadata dictionaries, either of which may be NULL, the null
object, or valid dictionaries. If required, additionally checks that the Info dictionary is
conformant. If 'enforcementLevel' is not PDF_ENFORCE_UNKNOWN, then it will be
used in preference to any entries in the info dictionary.
*/
Bool pdfxInitialise(PDFCONTEXT *pdfc, OBJECT *infoDict, OBJECT *metadataDict)
{
  /* Info dictionary match id's. */
  enum {
    idmTitle = 0,
    idmCreationDate,
    idmModDate,
    idmPDFXVersion,
    idmPDFXConformance,
    idmPDFXReportAllDeviations,
    idmMax
  };
  /* Metadata dictionary match id's. */
  enum {
    mdmPDFXVersion,
    mdmTrapped,
    mdmMax
  };
  NAMETYPEMATCH infoDictMatch[] = {
    {NAME_Title                   | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    {NAME_CreationDate            | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    {NAME_ModDate                 | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    {NAME_GTS_PDFXVersion         | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    {NAME_GTS_PDFXConformance     | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    {NAME_PDFXReportAllDeviations | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
    DUMMY_END_MATCH};
  NAMETYPEMATCH metadataDictMatch[] = {
    {NAME_pdfxid_GTS_PDFXVersion | OOPTIONAL, 1, {OSTRING}},
    {NAME_pdf_Trapped            | OOPTIONAL, 2, {ONAME, OSTRING}},
    DUMMY_END_MATCH};

  OBJECT* pdfxVersionFromInfoDict = NULL;
  OBJECT* pdfxConformance = NULL;
  OBJECT* pdfxVersionFromMetadata = NULL;
  int32 i;

  GET_PDFXC_AND_IXC;

  /* Initialise pdfx status variables. */
  OCopy(ixc->PDFXVersionValid, tnewobj);

  /* Clear the warning suppression flags - initially no warnings should be
  supressed. */
  for (i = 0; i < NUM_PDFXERRS; i ++)
    ixc->enforce_pdf_warnings[i] = FALSE;
  /* By default, individual warnings are only reported once. This is overridden
  by the presence of the PDFXReportAllDeviations key in the info dictionary. */
  ixc->suppress_duplicate_warnings = TRUE;

  /* Allocate and set the PDFX Version to (None) - this will be updated if an
  actual version is found. */
  if (! setVersionClaimed(pdfc, STRING_AND_LENGTH("(None)")))
    return FALSE;

  /* Default to not enforcing anything. */
  ixc->conformance_pdf_version = 0;

  /* If we're not bothered about PDF/x at all, stop now. */
  if (ixc->EnforcePDFVersion == PDF_ACCEPT_ANY)
    return TRUE;

  if (infoDict != NULL) {
    if (! pdf_dictmatch(pdfc, infoDict, infoDictMatch))
      return FALSE;
    pdfxVersionFromInfoDict = infoDictMatch[idmPDFXVersion].result;
    pdfxConformance = infoDictMatch[idmPDFXConformance].result;
  }

  if (metadataDict != NULL) {
    if (! pdf_dictmatch(pdfc, metadataDict, metadataDictMatch))
      return FALSE;
    pdfxVersionFromMetadata = metadataDictMatch[mdmPDFXVersion].result;
  }

  /* Read the version strings. */
  if (! determinPDFXVersion(pdfc, pdfxVersionFromInfoDict, pdfxConformance,
                            pdfxVersionFromMetadata))
    return FALSE;

  reportPDFVersion(ixc);

  /* Now we know the version, do we need to do any more PDF/X checks? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  if (infoDict != NULL) {
    /* Should duplicate warnings be supressed? */
    if (infoDictMatch[idmPDFXReportAllDeviations].result != NULL &&
        oBool(*infoDictMatch[idmPDFXReportAllDeviations].result))
      ixc->suppress_duplicate_warnings = FALSE;

    /* Check the required Info dictionary keys. */
    if (infoDictMatch[idmTitle].result == NULL)
      if (! pdfxError(ixc, PDFXERR_TITLE_KEY_MISSING))
        return FALSE;
    if (infoDictMatch[idmCreationDate].result == NULL)
      if (! pdfxError(ixc, PDFXERR_CREATION_DATE_MISSING))
        return FALSE;
    if (infoDictMatch[idmModDate].result == NULL)
      if (! pdfxError(ixc, PDFXERR_MODIFICATION_DATE_MISSING))
        return FALSE;
  }

  /* We'll prefer the metadata dictionary over the info dictionary, but not if
  the Trapped key is missing.
  This operation will mean we take the Trapped flag from the metadata for PDF/X
  jobs only, at least for the moment. */
  if (metadataDict != NULL && metadataDictMatch[mdmTrapped].result != NULL)
    pdf_setTrapStatus(pdfc, metadataDictMatch[mdmTrapped].result);

  if (! pdfxCheckTrapped(pdfc))
    return FALSE;

  /* The ixc->pdftrailer is valid at this point, so we may as well check it
  for conformance now. */
  if (! pdfxCheckTrailerDictionary(pdfc, &ixc->pdftrailer))
    return FALSE;

  return TRUE;
}

/* Log stripped */

