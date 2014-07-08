/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfx.h(EBDSDK_P.1) $
 * $Id: src:pdfx.h,v 1.18.2.1.1.1 2013/12/19 11:25:14 anon Exp $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Types and Interfaces for PDF/X conformance testing.
 */

#ifndef __pdfx_h__
#define __pdfx_h__

#include "swpdf.h"
#include "pdfattrs.h"

/** Public Methods **/

/* --Query Methods-- */

/**
 * \return TRUE if the file is a PDF/X version which allows external files.
 */
Bool pdfxExternalFilesAllowed(PDFCONTEXT* pdfc);

/* Conformance testing methods.

These methods should be called at appropriate times in the processing of a PDF
job.

Each of these methods returns FALSE only when a runtime error is detected;
they will generally return TRUE, even if conformance is not met (that error is
handled elsewhere).
*/

/** This should be the first conformance testing method called, and should be
passed a document info dictionary (which may be null if absent), and a document
metadata dictionary (which may be null if absent), as derived from the document
metadata stream. */
Bool pdfxInitialise(PDFCONTEXT *pdfc, OBJECT *infoDict, OBJECT *metadataDict);

/** This method should be called when an OutputIntents array is found in a
document Catalog dictionary.
If a DestOutputProfile isn't present, a profile in the ICC registry will be
opened and will remain open until the PDF job has completed. */
Bool pdfxProcessOutputIntents(PDFCONTEXT *pdfc, OBJECT *intentsArray);

/** This method closes a registry profile that might have been opened in
pdfxProcessOutputIntents when the DestOutputProfile isn't present. */
Bool pdfxCloseRegistryProfile(PDFCONTEXT *pdfc);

/** This method should be called when an unknown operator is encountered. */
Bool pdfxUnknownOperator(PDFCONTEXT *pdfc);

/** This method should be called once a pre-separated job has been detected. */
Bool pdfxPreseparatedJob(PDFCONTEXT *pdfc);

/** This method should be called whenever an Action is detected. */
Bool pdfxActionDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever a soft-masked image is detected. */
Bool pdfxSoftMaskedImageDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever an external font (i.e. one that is not
embedded in the PDF file) is detected. 'fileMissing', when true, indicates that
the font file is missing, otherwise it is assumed that the font descriptor for a
type 3 font is missing. */
Bool pdfxExternalFontDetected(PDFCONTEXT *pdfc, Bool fileMissing);

/** This method should be called whenever an OPI image replacement is detected.
*/
Bool pdfxOPIDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever a file specification is detected. */
Bool pdfxFileSpecificationDetected(PDFCONTEXT *pdfc, Bool embeddedFile);

/** This method should be called whenever a Reference XObject is detected. */
Bool pdfxReferenceXObjectDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever a halftone specifies a HalftoneName. */
Bool pdfxHalftoneNameDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever the PS operator is detected. */
Bool pdfxPSDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever an Postscript XObject is detected. */
Bool pdfxPSXObjectDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever a 16-bit image is detected. */
Bool pdfx16BitImageDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever a PDF 1.5 object stream is detected. */
Bool pdfxObjectStreamDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever a PDF 1.5 Cross-reference stream is
detected. */
Bool pdfxXrefStreamDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever the 'F' key is found in a stream
dictionary. */
Bool pdfxFKeyInStreamDictionaryDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever optional content is detected. */
Bool pdfxOptionalContentDetected(PDFCONTEXT *pdfc);

/** This method should be called whenever a PresSteps key in a page object is
detected. */
Bool pdfxPresStepsDictionaryDetected(PDFCONTEXT* pdfc);

/**
 * External output profiles are only allowed in X4p, X5n, and X5pg.
 * \param internalProfileFound true if an internal profile was found in
 *        addition to the external profile.
 */
Bool pdfxExternalOutputProfileDetected(PDFCONTEXT* pdfc,
                                       Bool internalProfileFound);

/** This method should be called for all halftone; 'type' should be the
numerical halftone type. */
Bool pdfxCheckHalftoneType(PDFCONTEXT *pdfc, int32 type);

/** This method should be called whenever a stream filter is created. */
Bool pdfxCheckFilter(PDFCONTEXT *pdfc, FILELIST *flptr, STACK *stack);

/** This method should be called when an image has a valid alternates array. */
Bool pdfxCheckImageAlternates(PDFCONTEXT *pdfc, OBJECT *alternatesArray);

/** This method should be called for all annotations. */
Bool pdfxCheckAnnotation(PDFCONTEXT *pdfc, OBJECT *annotationDict);

/** This method should be called for all TrapNet annotations. */
Bool pdfxCheckTrapNet(PDFCONTEXT *pdfc, OBJECT *pDictObj);

/** This method should be called once the various bounding boxes (e.g. Media
Box, Trim Box, etc) for a page have been determined. This method should be
called before pdfxCheckAnnotationBounds() is called for any annotations on the
same page, as bounds information is cached internally by this method. */
Bool pdfxCheckPageBounds(PDFCONTEXT *pdfc, PDF_PAGEDEV *pagedev);

/** This method should be called for all annotations in a job. */
Bool pdfxCheckAnnotationBounds(PDFCONTEXT *pdfc, OBJECT* annotationType,
                               OBJECT* annotationRectangle);

/** This method should be called for whenever a colorspace is set. The parent
space name should be NULL_COLORSPACE when not available, or a valid colorspace
name when spaceName is an alternate colorspace (e.g. the base space for an
/Index colorspace). */
Bool pdfxCheckColorSpace(PDFCONTEXT *pdfc, int32 spaceName,
                         int32 parentSpaceName);

/** This method should be called when a default colorspace substitution takes
place. */
Bool pdfxCheckDefaultColorSpace(PDFCONTEXT *pdfc, int32 colorSpaceName);

/** This method should be called with either of the 'rg' or 'RG' operators are
encountered. */
Bool pdfxCheckRgOperator(PDFCONTEXT *pdfc, Bool stroking);

/** This method should be called whenever an ExtGState is set. */
Bool pdfxCheckExtGState(PDFCONTEXT *pdfc, OBJECT *extGStateDict);

/** This method should be called whenever a Transparency Group is detected. */
Bool pdfxCheckTransparencyGroup(PDFCONTEXT *pdfc, OBJECT *groupDictionary);

/** This method should be called when the page-group colorspace is being
processed; pdfx may need to override it. */
Bool pdfxCheckForPageGroupCSOverride(PDFCONTEXT *pdfc,
                                     OBJECT* pageGroupColorspace,
                                     OBJECT* overrideColorspace);

/** This method should be called at the start of a job when the document catalog
dictionary is available. */
Bool pdfxCheckCatalogDictionary(PDFCONTEXT* pdfc, OBJECT* catalogDictionary);

/** This method should be called whenever a rendering intent is set. */
Bool pdfxCheckRenderingIntent(PDFCONTEXT* pdfc, OBJECT* intentName);

/** In the file trailer dictionary, the Encrypt entry is not permitted, and the
ID entry is required. */
Bool pdfxCheckTrailerDictionary(PDFCONTEXT *pdfc, OBJECT *trailerDict);

/** 'Trapped' must be present in an info dictionary, and must be True or False.
*/
Bool pdfxCheckTrapped(PDFCONTEXT *pdfc);

/**
 * All keys must be present in a DestOutputProfileRef dictionary. Additionally
 * it should not contain the ColorantTable key.
 */
Bool pdfxCheckDestOutputProfileRef(PDFCONTEXT* pdfc, OBJECT* refDictionary);

/**
 * Check that the passed dictionary is a URL specification, containing only
 * /FS and /F keys.
 */
Bool pdfxCheckUrl(PDFCONTEXT* pdfc, OBJECT* urlDictionary);

/**
 * Conformance Level: All
 *
 * Check that the ID elements of an external file match those given in an
 * external XObject reference.
 *
 * \param externalFile The external file; this is only used to report the name
 *        of the referenced file.
 * \param xObjectMetadata The metadata stream in the source XObject.
 * \param externalDocMetadata The parsed metadata of the target document.
 * \param refId The ID in the reference dictionary.
 * \param externalDocTrailer The trailer dictionary of the target document.
 * \return FALSE on error.
 */
Bool pdfxCheckExternalFileId(PDFCONTEXT* pdfc,
                             FILELIST* externalFile,
                             OBJECT* xObjectMetadata,
                             DocumentMetadata* externalDocMetadata,
                             OBJECT* refId,
                             OBJECT* externalDocTrailer);

#endif

/* Log stripped */

