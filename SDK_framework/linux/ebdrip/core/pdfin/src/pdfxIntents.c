/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfxIntents.c(EBDSDK_P.1) $
 * $Id: src:pdfxIntents.c,v 1.37.1.1.1.1 2013/12/19 11:25:13 anon Exp $
 *
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The file is responsible for checking and using the output intent specified
 * by a PDF/X job.
 */

#include "core.h"
#include "pdfxPrivate.h"
#include "pdfx.h"

#include "pdfin.h"              /* conformance_pdf_version */
#include "pdfmatch.h"           /* pdf_dictmatch */
#include "pdfmem.h"             /* pdf_create_string */
#include "pdftextstr.h"         /* pdf_text_to_utf8 */
#include "pdfxref.h"            /* pdf_lookupxref */
#include "pdffs.h"

#include "gschcms.h"            /* gsc_addOutputIntent */
#include "gsc_icc.h"            /* gsc_get_icc_output_profile_device_space */
#include "gscdevci.h"           /* gsc_getoverprintmode */
#include "gstack.h"             /* gstateptr */
#include "hqmemcmp.h"           /* HqMemCmp */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "swerrors.h"           /* VMERROR */
#include "hqnuri.h"
#include "swcopyf.h"
#include "params.h"

/* ----------------------------------------------------------------------------
** pdf_FindRegisteredProfile()
** Given the file name of a resident registered profile (looked up from the
** list of registered ICC profiles), open it.
*/
static Bool pdf_FindRegisteredProfile( OBJECT *pFileName, OBJECT *pRetFile )
{
  int32 openFlags = SW_RDONLY;
  int32 psFlags = READ_FLAG;

  if (!file_open( pFileName, openFlags, psFlags, FALSE, 0, pRetFile ))
    return FALSE;

  return TRUE;
}


/* ----------------------------------------------------------------------------
** pdf_GetListString()
** Returns the next string from the list of registered ICC profiles (see
** comment for pdf_CheckRegProfile()).  The file's text format (syntax) is:-
**  Valid entries contain an ID string followed by a file spec. string.
**  Comment character '%' starts a comment (except when given in a
**  string) which continues to the end of the line.
**  The ICC registry ID is given as a PostScript string (it may contain
**  spaces). The '\' escape character can be used if a round-bracket
**  character ( '(' or ')' ) is required as part of the ID string.
**  Similarly, the SW file specification is also given as a string.  The file
**  specification must be given on the same line as the corresponding ID.
**  to show that no such file exists, an empty string must be given.
*/
static Bool pdf_GetListString( FILELIST *pFl, uint8 *pBuff, int32 stopAtEOLN )
{
  int32 ci = EOF;
  char  ch;
  int32 inString = FALSE;
  int32 escapedChar = FALSE;

  /* Read through the file, ignoring comments, until a string is obtained.
     If 'stopAtEOLN' is true, then we're being called just after having
     previously returned an ID string and so the file spec string is expected
     before EOLN. */
  while ((ci = Getc( pFl )) != EOF) {
    ch = (char) ci;
    if (ch == '%') {                    /* Start of a comment?,        */
      if (inString)                     /* but if in a string          */
        *pBuff++ = ch;                  /* keep as part of the string, */
      else {                            /* otherwise skip to EOLN.     */
        while ((ci = Getc( pFl )) != EOF) {
          ch = (char) ci;
          if (ch == '\n' || ch == '\r')
            break;
        }
        if (stopAtEOLN) {
          *pBuff = '\0';
          return TRUE;
        }
      }
      escapedChar = FALSE;
    } else if (ch == '\\') {            /* If using the escape char,  */
      if (escapedChar && inString)      /* store in string if already */
        *pBuff++ = ch;                  /* escaped,                   */
      escapedChar = !escapedChar;       /* and set the escape state.  */
    } else {
      if (ch == '\n' || ch == '\r') {   /* If reached EOLN,           */
        if (inString || stopAtEOLN) {
          *pBuff = '\0';
          return TRUE;
        }
        inString = FALSE;               /* not in string any more.    */
      } else if (ch == '(') {           /* Start of string, or...     */
        if (escapedChar && inString)    /* part of one already.       */
          *pBuff++ = ch;
        else
          inString = TRUE;
      } else if (ch == ')') {           /* End of string, or...       */
        if (escapedChar && inString)    /* part of one already.       */
          *pBuff++ = ch;
        else {                          /* Definitely end of string.  */
          *pBuff = '\0';                /* Terminate it.              */
          return TRUE;                  /* Normal successful exit point.*/
        }
      } else {                          /* For all bog standard chars,*/
        if (inString)                   /* copy thru if in a string.  */
          *pBuff++ = ch;
      }
      escapedChar = FALSE;
    }
  }

  return FALSE;     /* No valid string obtained. */
}

/* ----------------------------------------------------------------------------
** pdf_CheckListICCfile()
** See comments for pdf_CheckRegProfile() - parameters and return conditions
** are more or less the same.
*/
static Bool pdf_CheckListICCfile( PDFCONTEXT *pdfc,
                                  FILELIST *pFl,     /* Ptr to text file.    */
                                  OBJECT *pOCI,      /* ICC ID to match on.  */
                                  OBJECT *pICCfile ) /* Return ICC file name.*/
{
  int32 matched = FALSE;
  uint8 buff[256];

  /* Read through pairs of strings from the file until either a match
     on the ID or EOF. */
  while (!matched && pdf_GetListString( pFl, buff, FALSE )) {
    int32 len = strlen_int32( (char*) buff );
    if (len > 0) {
      if (HqMemCmp( oString(*pOCI), theLen(*pOCI), buff, len ) == 0) {
        matched = TRUE;
        theTags(*pICCfile) = OSTRING;
        theLen(*pICCfile) = 0;
      }
    }

    /* Read the file spec. */
    if (pdf_GetListString( pFl, buff, TRUE )) {

      /* If matched on the ID, and the file spec is present, return
         the file spec. */
      len = strlen_int32( (char*) buff );
      if (matched && len > 0) {
        if (!pdf_create_string( pdfc, len, pICCfile ))
          return FALSE;

        HqMemCpy( oString(*pICCfile), buff, len );
      }
    }
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdfxInitOverprints()
** Initialises the overprints to default values. Some params have defaults in
** the PDF spec, others are Hqn extensions.
*/
static Bool pdfxInitOverprints(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  /* The overprint settings should match the default PDF settings; if any of
  these have been overridden we'll issue a PDF/X error. */
  if (gsc_getignoreoverprintmode(gstateptr->colorInfo) ||
      gsc_getoverprintmode(gstateptr->colorInfo) ||
      gsc_getoverprintblack(gstateptr->colorInfo) ||
      gsc_getoverprintgray(gstateptr->colorInfo) ||
      gsc_getoverprintgrayimages(gstateptr->colorInfo) ||
      !gsc_getoverprintwhite(gstateptr->colorInfo)) {

    if (!gsc_setignoreoverprintmode(gstateptr->colorInfo, FALSE) ||
        !gsc_setoverprintmode(gstateptr->colorInfo, FALSE) ||
        !gsc_setoverprintblack(gstateptr->colorInfo, FALSE) ||
        !gsc_setoverprintgray(gstateptr->colorInfo, FALSE) ||
        !gsc_setoverprintgrayimages(gstateptr->colorInfo, FALSE) ||
        !gsc_setoverprintwhite(gstateptr->colorInfo, TRUE))
      return FALSE;

    if (! pdfxError(ixc, PDFXERR_OVERPRINT_OVERRIDDEN))
      return FALSE;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdfxInitColorManagement()
** Initialises the input side of colour management to default values. The
** rendering intent has a default in the PDF spec, while the intercepts are
** Hqn extensions and are turned off.
*/
static Bool pdfxInitColorManagement(PDFCONTEXT *pdfc)
{
  GET_PDFXC_AND_IXC;

  if ((ixc->conformance_pdf_version & PDF_CHECK_COLORMANAGEMENT_REQUIRED) != 0) {
    OBJECT nameObj = OBJECT_NOTVM_NAME(NAME_RelativeColorimetric, LITERAL);

    /* Turn off all interception and force overrides off */
    if (!gsc_pdfxResetIntercepts(gstateptr->colorInfo))
      return FALSE;

    /* Set the initial rendering intent of all PDF/X jobs to relative colorimetric.
     * Don't bother with a warning for this because it is a corollary of turning
     * off the overrides.
     */
    if (!gsc_setrenderingintent(gstateptr->colorInfo, &nameObj))
      return FALSE;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdf_CheckRegProfile()
** The ID of a registered ICC profile (via *pOCI) is checked against a
** list of such IDs.  If found in the list, the corresponding SW name of the
** ICC profile file (if given in the list) is returned (via *pICCfile).
** oType(*pICCfile) is returned as ONULL if no match found.
** oType(*pICCfile) is returned as OSTRING if an ID match is found, but with
** a zero length if no file name was present.
*/
static Bool pdf_CheckRegProfile( PDFCONTEXT *pdfc,
                                 OBJECT *pOCI,
                                 OBJECT *pICCfile )
{
  OBJECT FName = OBJECT_NOTVM_NOTHING;
  OBJECT FileObj = OBJECT_NOTVM_NOTHING;
  int32 openFlags = SW_RDONLY;
  int32 psFlags = READ_FLAG;
  FILELIST *pFl;
  Bool ret = TRUE;
  GET_PDFXC_AND_IXC;

  theTags(*pICCfile) = ONULL;     /* Default: return no match */

  /* Validate the OuputConditionIdentifier as a string. */
  if ((theLen(*pOCI)== 0) || (oString(*pOCI) == NULL))
    return TRUE;

  /* Open the file containing the list of registered profiles. */
  theTags(FName) = OSTRING | LITERAL | READ_ONLY;
  oString(FName) = (uint8*) "iccreg/Profiles";
  theLen(FName) = strlen_uint16( (char*) oString(FName) );

  if (!file_open( &FName, openFlags, psFlags, FALSE, 0, &FileObj )) {
    if ((ixc->conformance_pdf_version &
         PDF_CHECK_COLORMANAGEMENT_REQUIRED) != 0)
      monitorf( UVS("Warning: No SW/iccreg/Profiles file.\n") );
    return TRUE;
  }

  pFl = oFile(FileObj);
  if (pFl != NULL) {

    /* Read through the file until an ID matches *pOCI or EOF. */
    ret = pdf_CheckListICCfile( pdfc, pFl, pOCI, pICCfile );

    /* Close the list file. */
    if (!file_close( &FileObj ))
      return FALSE;

  } else {
    HQFAIL( "Failed to find registered ICC profiles list file." );
  }

  return ret;
}

/* Process a PDF/X OutputIntent dictionary for its ICC profile.
*/
static Bool processOutputIntent( PDFCONTEXT *pdfc , OBJECT* outputCondition ,
                                 OBJECT* conditionID , OBJECT* profile ,
                                 OBJECT* registry )
{
  Bool fromICCregistry = FALSE;
  Bool fOpenedFile = FALSE;
  OBJECT Result = OBJECT_NOTVM_NOTHING;
  OBJECT ICCfile = OBJECT_NOTVM_NOTHING;
  GET_PDFXC_AND_IXC;

  /* The output condition is only optional for pre-2003 specs. */
  if ((ixc->conformance_pdf_version &
       (PDF_ENFORCE_X1a_2001 | PDF_ENFORCE_X3_2002)) == 0 &&
      outputCondition == NULL)
    if (! pdfxError(ixc, PDFXERR_NO_OUTPUT_CONDITION))
      return FALSE;

  /* This is required for checks of PDF/X-3 when verifying colorspaces in the job */
  ixc->PDFXOutputProfilePresent = (profile != NULL);

  /* Try to match the OutputConditionIdentifier against the list of known
     registered ICC profile IDs. */
  if (! pdf_CheckRegProfile( pdfc, conditionID, &ICCfile ))
    return FALSE;

  if (registry != NULL) {
    /* The RegistryName is required to be www.color.org if there's no DestOutputProfile. */
    if (HqMemCmp(oString(*registry), theLen(*registry),
                 STRING_AND_LENGTH("http://www.color.org")) == 0) {
      fromICCregistry = TRUE;
    }
  }

  if (oType(ICCfile) == OSTRING && fromICCregistry) {  /* did match a registered ICC ID */

    /* If DestOutputProfile is not present, then use the registered
       profile (if present). */
    if (profile == NULL) {
      /* If the length of the filename is zero, there is no file present. */
      if (theLen(ICCfile) > 0) {
        if (pdf_FindRegisteredProfile( &ICCfile, &Result )) {
          fOpenedFile = TRUE;
          profile = &Result;
          ixc->PDFXRegistryProfile = *profile;
        }
        else {
          /* Unable to open the file - the 'SW/iccreg/Profiles' file must be
          out of date. */
          HQFAIL("processOutputIntent - The file 'SW/iccreg/Profiles' is "
                 "out-of-date - it refers to a file that cannot be opened.");

          if (! pdfxError( ixc, PDFXERR_NO_PROFILE_FILE ))
            return FALSE;
          monitorf(UVM("Missing ICC profile filename: %s"), theLen(ICCfile),
                   oString(ICCfile));
        }
      }
      else {
        /* The registered ID does not have an associated file. */
        if ((ixc->conformance_pdf_version &
             PDF_CHECK_COLORMANAGEMENT_REQUIRED) != 0)
          if (! pdfxError( ixc, PDFXERR_NO_REG_PROFILE ))
            return FALSE;
      }
    }
  } else {
    /* We did not match a registered name; the DestOutputProfile should be
       present. */
    if (profile == NULL)
      if (! pdfxError( ixc, PDFXERR_NO_DEST_OUTPUT_PROFILE ))
        return FALSE;

    /* The OutputCondition key is required in PDF/X-1a:2001 only.  */
    if ((ixc->conformance_pdf_version & PDF_CHECK_X1a_2001_ONLY) != 0 &&
        outputCondition == NULL) {
      if (! pdfxError( ixc, PDFXERR_NO_OUTPUT_CONDITION ))
        return FALSE;
    }
  }

  /* Display the printing condition name a.k.a. "production condition" or
     "intended output device", depending on the weather! :-(  */
  {
    OBJECT *pObj = outputCondition;
    if (pObj == NULL)
      pObj = conditionID;
    if (pObj != NULL) {
      HQASSERT((oType(*pObj) == OSTRING), "processOutputIntent: printing condition not a string.");
      if ( (oType(*pObj) == OSTRING) && (theLen(*pObj) > 0) ) {
        int32 ret;
        int32 buffer_size = 3*theLen(*pObj);
        utf8_buffer text_string;
        text_string.codeunits = mm_alloc(mm_pool_temp, buffer_size, MM_ALLOC_CLASS_PDF_TEXTSTRING);
        if ( text_string.codeunits == NULL ) {
          return error_handler(VMERROR) ;
        }
        ret = pdf_text_to_utf8(pdfc, pObj, buffer_size, &text_string);
        if ( ret ) {
          /* UVM("Identified PDF/X output printing condition - %s%s") */
          monitorf((uint8*)"Identified PDF/X output printing condition - ");
          monitorf((uint8*)"%s%.*s\n", UTF8_BOM, text_string.unitlength, text_string.codeunits);

          /* Copy the string to PDF memory. */
          if (pdf_create_string(pdfc,theLen(*pObj), &ixc->PDFXOutputCondition))
            HqMemCpy(oString(ixc->PDFXOutputCondition), oString(*pObj),theLen(*pObj));
          else
            ret = FALSE;
        }
        mm_free(mm_pool_temp, text_string.codeunits, buffer_size);
        if ( !ret ) {
          return FALSE ;
        }
      }
    }
  }

  /* If 'profile' is not null, then it points either to a registered profile
     or to a given (embedded) DestOutputProfile profile. */
  if (profile != NULL) {
    /* Actually try to use the profile if the conformance level requires it.
     * It is possible that we will need to use the profile in the registry cache
     * for color management of PDF/X-3 to a proofing even if it isn't required
     * otherwise.
     */
    if ((ixc->conformance_pdf_version &
         PDF_CHECK_COLORMANAGEMENT_REQUIRED) != 0) {

      /* If the file is PDF/X-4, we need to get hold of the device space of the
      output condition; we'll use this for the page group space if one is not
      specified in the job. */
      if ((ixc->conformance_pdf_version & PDF_CHECK_ANY_X4) != 0) {
        if (! gsc_get_icc_output_profile_device_space(
                gstateptr->colorInfo,
                profile, &ixc->pdfxState.conditionDeviceSpace))
          return FALSE;
        /* Note that we'll check the condition device space for validity when we
        try to use it; see pdfxCheckForPageGroupCSOverride(). */
      }

      if (!gsc_addOutputIntent( gstateptr->colorInfo, profile ))
        return FALSE;

      /* Now that we know an output profile is being used, initialise overprints,
       * rendering intent and colour management intercepts.
       */
      if (!pdfxInitColorManagement(pdfc))
        return FALSE;
    }
  }

  return TRUE;
}

Bool pdfxCloseRegistryProfile(PDFCONTEXT *pdfc)
{
  Bool ret = TRUE;
  GET_PDFXC_AND_IXC;

  if (oType(ixc->PDFXRegistryProfile) != ONULL)
    ret = file_close( &ixc->PDFXRegistryProfile );

  object_store_null(&ixc->PDFXRegistryProfile);

  return ret;
}

/**
 * Identify the leaf name within the passed URL.
 * For example, the leaf name of "http://dir1/test.pdf?a=1#anchor" is
 * "test.pdf".
 *
 * \param url The URL.
 * \param result A postscript string will be stored here. The string will be
 *        automatically deleted on the next restore or GC.
 * \return FALSE on error.
 */
static Bool getUrlLeafName(OBJECT* url, OBJECT* result)
{
  int32 start, length, decodedLength;
  uint8* chars = oString(*url);

  HQASSERT(oType(*url) == OSTRING, "Expected url to be a string.");

  hqn_uri_find_leaf_name(chars, theLen(*url), &start, &length);
  if (length == 0) {
    *result = onull;
    return TRUE;
  }

  chars = chars + start;
  if (hqn_uri_validate_percent_encoding(chars, length, &decodedLength)) {
    if (! ps_string(result, NULL, decodedLength))
      return FALSE;

    hqn_uri_percent_decode(chars, length, oString(*result), decodedLength);
    return TRUE;
  }
  else {
    /* If the percent encoding is invalid just use the string as-is. */
    return ps_string(result, chars, length);
  }
}

/**
 * Generate an MD5 for the passed file.
 *
 * \param file The file to digest; no seek will be performed on this file, so
 *        if the full file digest is required ensure it is rewound before
 *        calling this method.
 * \param md5 This will be filled with the calculated digest.
 * \return FALSE on error.
 */
static Bool md5File(FILELIST* file, uint8 md5[16])
{
#define BUFFER_SIZE 64
  uint8 buffer[BUFFER_SIZE];
  int32 available;
  uint32 totalLength = 0;

  do {
    if (file_read(file, buffer, BUFFER_SIZE, &available) == 0) {
      return FALSE;
    }
    /* Note the we can build total length up like this because it's only used
     * in the last call to md5_progressive. */
    totalLength += available;
    md5_progressive(buffer, available, md5, totalLength,
                    totalLength <= BUFFER_SIZE, available < BUFFER_SIZE);
  } while (available == BUFFER_SIZE);

  return TRUE;
}

/**
 * Calculate the MD5 checksum of the passed external profile, and compare it to
 * that passed, and raise a PDF/X error if the two do not match.
 *
 * \param externalProfile The external profile to checksum. The file will be
 *        rewound before and after being checksummed.
 * \param expectedChecksum The expected checksum, as a 16-byte string.
 * \return FALSE on error.
 */
static Bool compareProfileChecksums(PDFCONTEXT* pdfc,
                                    FILELIST* externalProfile,
                                    OBJECT* expectedChecksum)
{
  uint8 md5[16];
  Hq32x2 zero = {0, 0};
  Bool match = FALSE;
  GET_PDFXC_AND_IXC;

  HQASSERT(oType(*expectedChecksum) == OSTRING &&
           theLen(*expectedChecksum) == 16,
           "'expectedChecksum' should be a 16-byte string.");

  if (! (externalProfile->mysetfileposition(externalProfile, &zero) ||
         md5File(externalProfile, md5) ||
         externalProfile->mysetfileposition(externalProfile, &zero))) {
    return FALSE;
  }

  match = HqMemCmp(md5, 16, oString(*expectedChecksum),
                   theLen(*expectedChecksum)) == 0;

  if (! match) {
    if (! pdfxError(ixc, PDFXERR_EXTERNAL_PROFILE_CHECKSUM_DIFFERS)) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * Find an external profile.
 *
 * \param refDict A DestOutputProfileRef dictionary, as defined by the PDF/X-4p
 *        specification.
 * \param resultFileObject This object will contain the FILE object of the
 *        profile if it was found, or ONULL if not.
 * \return FALSE on error.
 */
static Bool findExternalProfile(PDFCONTEXT* pdfc, OBJECT* refDict,
                                OBJECT* resultFileObject)
{
  enum { urlsMatch, checksumMatch };
  NAMETYPEMATCH profileRefMatch[] = {
    { NAME_URLs, 2, { OARRAY, OINDIRECT }},
    { NAME_CheckSum | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
    DUMMY_END_MATCH };
  NAMETYPEMATCH urlMatch[] = {
    { NAME_F, 2, { OSTRING, OINDIRECT }},
    { NAME_FS | OOPTIONAL, 2, { ONAME, OINDIRECT }},
    DUMMY_END_MATCH };
  OBJECT* urlArray;
  int32 i;
  GET_PDFXC_AND_IXC;

  *resultFileObject = onull;

  if (! pdfxCheckDestOutputProfileRef(pdfc, refDict))
    return FALSE;

  if (! pdf_dictmatch(pdfc, refDict, profileRefMatch))
    return FALSE;

  urlArray = profileRefMatch[urlsMatch].result;
  if (urlArray != NULL) {
    for (i = 0; i < theLen(*urlArray); i ++) {
      OBJECT* urlDict = &oArray(*urlArray)[i];
      OBJECT urlLeafName = OBJECT_NOTVM_NULL;
      OBJECT fileName = OBJECT_NOTVM_NULL;

      if (oType(*urlDict) != ODICTIONARY) {
        if (! pdfxError(ixc, PDFXERR_EXTERNAL_PROFILE_NON_DICTIONARY_URL))
          return FALSE;
      }
      else {
        if (! pdfxCheckUrl(pdfc, urlDict))
          return FALSE;

        if (! pdf_dictmatch(pdfc, urlDict, urlMatch))
          return FALSE;

        /* pdf_locate_file() does not support URLs, so trim to just the leaf name. */
        if (! getUrlLeafName(urlMatch[0].result, &urlLeafName))
          return FALSE;

        /* Note that for PDF/X jobs, we only look for external files using the
         * OPI system. */
        if (! pdf_locate_file(pdfc, &urlLeafName, &fileName, resultFileObject,
                              pdfxExternalFilesAllowed(pdfc)))
          return FALSE;

        if (oType(*resultFileObject) == OFILE) {
          OBJECT* checksum = profileRefMatch[checksumMatch].result;

          if (checksum != NULL) {
            if (oType(*checksum) != OSTRING || theLen(*checksum) != 16) {
              if (! pdfxError(ixc, PDFXERR_EXTERNAL_PROFILE_INVALID_CHECKSUM)) {
                return FALSE;
              }
            }
            else {
              if (ixc->PDFXVerifyExternalProfileCheckSums) {
                if (! compareProfileChecksums(pdfc, oFile(*resultFileObject),
                                              checksum)) {
                  return FALSE;
                }
              }
            }
          }
          break;
        }
        else {
          /* UVM("Failed to find PDF/X external profile: %s") */
          monitorf((uint8*)"Failed to find PDF/X external profile: %.*s\n",
                   theLen(urlLeafName), oString(urlLeafName)) ;
        }
      }
    }
  }

  return TRUE;
}

/** Conformance Level: All

This method processes the Intents array, as present in an OutputIntents entry in
the document Catalog. It specifies the "intended" output condition for which the
PDF file was prepared; this is done by naming and/or providing an ICC profile.

This method checks the intent array for conformance, as well as installing the
output condition for the job via pdf_UseOutputIntent().
*/
Bool pdfxProcessOutputIntents(PDFCONTEXT *pdfc, OBJECT *intentsArray)
{
  enum {
    oi_Type, oi_S, oi_OutputCondition, oi_OutputConditionIdentifier,
    oi_RegistryName, oi_Info, oi_DestOutputProfile,
    oi_DestOutputProfileRef};
  NAMETYPEMATCH outputIntentMatch[] = {
    { NAME_Type              | OOPTIONAL,  2, { ONAME, OINDIRECT }},
    { NAME_S,                              2, { ONAME, OINDIRECT }},
    { NAME_OutputCondition   | OOPTIONAL,  2, { OSTRING, OINDIRECT }},
    { NAME_OutputConditionIdentifier,      2, { OSTRING, OINDIRECT }},
    { NAME_RegistryName      | OOPTIONAL,  2, { OSTRING, OINDIRECT }},
    { NAME_Info              | OOPTIONAL,  2, { OSTRING, OINDIRECT }},
    { NAME_DestOutputProfile | OOPTIONAL,  2, { OFILE, OINDIRECT }},
    { NAME_DestOutputProfileRef | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    DUMMY_END_MATCH };
  int32 i;
  Bool fSeen_S_Key = FALSE;
  GET_PDFXC_AND_IXC;

  /* Is checking required? */
  if (ixc->conformance_pdf_version == 0)
    return TRUE;

  /* The intents array must be present. */
  if (intentsArray == NULL)
    return pdfxError(ixc, PDFXERR_NO_OUTPUTINTENT);

  /* Iterate over the Output Intent dictionaries, looking for the entry who's
  /S key matches /GTS_PDFX. There may only be exactly one of these, and it will
  be the one used. */
  for (i = 0;  i < theLen(*intentsArray); i++) {
    OBJECT *pDictRef = &(oArray(*intentsArray)[i]);
    OBJECT *pDict = NULL;

    HQASSERT(pDictRef != NULL, "pdfxProcessOutputIntents - the intents array "
             "cannot contain null elements.");

    if (oType(*pDictRef) == OINDIRECT) {
      /* Resolve the indirect reference to the OutputIntent dictionary. */
      if (! pdf_lookupxref(pdfc, &pDict, oXRefID(*pDictRef),
                           theIGen(pDictRef), FALSE))
        return FALSE;

      if (pDict == NULL)
        continue;
    }
    else
      pDict = pDictRef;

    if (oType(*pDict) != ODICTIONARY)
      continue;

    if (! pdf_dictmatch(pdfc, pDict, outputIntentMatch))
      return FALSE;

    /* Make sure the objects don't get purged while we're still
       referencing them. */
    pdf_xrefexplicitaccess_dictmatch( pdfxc , outputIntentMatch ,
                                      pDict , TRUE ) ;

    /* If present, the Type key must = OutputIntent */
    if (outputIntentMatch[oi_Type].result != NULL)
      if (oNameNumber(*outputIntentMatch[oi_Type].result) != NAME_OutputIntent)
        return pdfxError(ixc, PDFXERR_BAD_OI_DICT_TYPE);

    /* The only known value for /S key is GTS_PDFX. The S key is mandatory. */
    if (oNameNumber(*outputIntentMatch[oi_S].result) == NAME_GTS_PDFX) {
      if (fSeen_S_Key) {
        if (! pdfxError(ixc, PDFXERR_MULTIPLE_GTS_PDFX))
          return FALSE;
        /* There's no point warning about any more S keys that may be present. */
        break;
      }
      else {
        OBJECT* profile = outputIntentMatch[oi_DestOutputProfile].result;
        OBJECT* profileRef = outputIntentMatch[oi_DestOutputProfileRef].result;
        OBJECT externalProfile = OBJECT_NOTVM_NOTHING;

        fSeen_S_Key = TRUE;

        /* Now that we know an output intent exists, initialise overprints.
         */
        if (!pdfxInitOverprints(pdfc))
          return FALSE;

        if (profileRef != NULL) {
          if (! pdfxExternalOutputProfileDetected(pdfc, profile != NULL))
            return FALSE;

          if (! findExternalProfile(pdfc, profileRef, &externalProfile)) {
            return FALSE;
          }

          if (oType(externalProfile) == ONULL) {
            profile = NULL;
          }
          else {
            profile = &externalProfile;
          }
        }

        /* Process the OutputIntent dictionary for its ICC profile. */
        if (! processOutputIntent(pdfc, outputIntentMatch[oi_OutputCondition].result,
                                  outputIntentMatch[oi_OutputConditionIdentifier].result,
                                  profile,
                                  outputIntentMatch[oi_RegistryName].result))
          return FALSE;
      }
    }
  }

  if (!fSeen_S_Key)
    return pdfxError(ixc, PDFXERR_NO_OUTPUTINTENT);

  return TRUE;
}


/* Log stripped */

