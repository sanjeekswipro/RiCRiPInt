/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfrefs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A PDF Reference XObject is a Form XObject with a /Ref entry in its stream
 * dictionary.  The file (genuinely external or embedded) named by the /Ref
 * entry is a complete PDF file.  The rendering of a single page from
 * the referenced file is controlled by routines contained here.
 *
 * pdfform_dispatch() in pdfxobj.c has recognised that the /Ref key is
 * specified in the Form's parameters dictionary and so calls the one entry
 * point to this module - pdf_Ref_dispatch().  Whatever the given file
 * specification boils down to (e.g. a proper file in its own right or an
 * embedded file), a stream-cum-file object is created to represent it.
 * 'pdf_exec_page()' is then invoked on the file to get it rendered.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"

#include "graphics.h"
#include "gstack.h"
#include "gu_ctm.h"
#include "gu_rect.h"
#include "swmemory.h"
#include "matrix.h"
#include "psvm.h"
#include "utils.h"
#include "monitor.h"

#include "swpdf.h"
#include "stream.h"
#include "pdfmatch.h"
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdffs.h"
#include "pdfexec.h"
#include "pdfin.h"
#include "pdfdefs.h"
#include "pdfrefs.h"
#include "pdfx.h"

/**
 * Find a file referenced by the passed PDF file reference.
 *
 * \param fileRef The OBJECT representing the value of the /F key in the PDF
 *        reference dictionary.
 * \param name Set to the name of the file, if it was not an embedded stream;
 *        otherwise set to ONULL.
 * \param file Set to the file handle if the file was found and opened.
 */
static Bool pdf_Ref_getfile( PDFCONTEXT *pdfc,
                             OBJECT *fileRef,
                             OBJECT *name,
                             FILELIST **file )
{
  OBJECT psfilename = OBJECT_NOTVM_NOTHING;
  OBJECT *ID;
  Bool isVolatile;
  OBJECT ResultFile = OBJECT_NOTVM_NOTHING;
  OBJECT RetFName = OBJECT_NOTVM_NOTHING;

  *file = NULL;  /* null indicates file not found to the caller */
  *name = onull;

  if (!pdf_filespec( pdfc, fileRef, &psfilename, &ID , &isVolatile ))
    return FALSE;

  if (oType(psfilename) == OFILE) {
    /* pdf_filespec() has located an embedded file stream as the resolution
       of the /F reference.  This is returned (and used) directly. */
    Copy( &ResultFile, &psfilename );

  } else {
    HQASSERT(oType(psfilename) == OSTRING, "String file name expected.");
    *name = psfilename;

    /* pdf_filespec() has resolved the /F reference to a file name string.
       Try to open the file (including a search through the OPI folders
       if necessary).
       Note that for PDF/X jobs, we only look for external files using the OPI
       system. */
    if (!pdf_locate_file(pdfc, &psfilename, &RetFName, &ResultFile,
                         pdfxExternalFilesAllowed(pdfc)))
      return FALSE;

    if (oType(RetFName) == ONULL  ||  oType(ResultFile) == ONULL)
      return TRUE;    /* File not found is not an "error". */
  }

  /* Some checks */
  *file = oFile( ResultFile ) ;
  if (!isIOpenFileFilter( &ResultFile, *file ) ||
      !isIInputFile( *file ) || isIEof( *file ))
    return error_handler( IOERROR );

  return TRUE;
}

/**
 * Execute a page in an external PDF file.
 *
 * \param file The external PDF file.
 * \param refDictionary The PDF reference dictionary.
 * \param xObjectMetadata Metadata associated with the reference XObject; may be
 *        null.
 * \param pageFound Set to true if the referenced page was present in the
 *        external PDF file.
 * \return FALSE on error.
 */
static Bool execExternalPage(PDFCONTEXT* parentPdfc,
                             FILELIST* file,
                             OBJECT* refDictionary,
                             OBJECT* xObjectMetadata,
                             Bool* pageFound)
{
  enum { page, id };
  NAMETYPEMATCH match[] = {
    { NAME_Page, 3, { OINTEGER, OSTRING, OINDIRECT }},
    { NAME_ID | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    DUMMY_END_MATCH };
  PDFCONTEXT *pdfc = NULL;
  Bool success;

  if (! pdf_dictmatch(parentPdfc, refDictionary, match))
    return FALSE;

  if (! pdf_open_refd_file(&pdfc, parentPdfc->pdfxc->u.i, file,
                           parentPdfc->corecontext))
    return FALSE;

  {
    GET_PDFXC_AND_IXC;
    /* Note that we use the parent pdfc as that defines the PDF/X conformance
     * level of the parent document, which is what we're enforcing. */
    success = pdfxCheckExternalFileId(parentPdfc, file, xObjectMetadata,
                                      &pdfc->pdfxc->metadata,
                                      match[id].result,
                                      &ixc->pdftrailer) &&
              pdf_exec_page(pdfc, match[page].result, pageFound);
  }

  if (! pdf_close_internal( pdfc ))
    return FALSE;

  return success;
}

/*-----------------------------------------------------------------------------
 * pdf_Ref_dispatch()
 *
 * Parameters:
 *  pdfc     - usual pdf context;
 *  pRefDict - the /Ref dictionary
 *  pBBox    - the Form XObject's bounding box
 *  pMatrix  - the From XObject's transformation matrix
 *  pMetadata - the Form XObject's Metadata; this may be null.
 *  pWasRendered - return a flag saying whether or not the referenced page and
 *                 and file were found ok and an attempt made to render it.
 */

Bool pdf_Ref_dispatch( PDFCONTEXT *pdfc,
                       OBJECT *pRefDict,
                       OBJECT *pBBox,
                       OBJECT *pMatrix,
                       OBJECT *pMetadata,
                       Bool   *pWasRendered )
{
  Bool   success;
  RECTANGLE cliprect;
  OMATRIX matrix;
  OBJECT  matrixArray = OBJECT_NOTVM_NOTHING;
  Bool    matrixAllocated = FALSE;
  FILELIST *pFileList;
  OBJECT  save_obj = OBJECT_NOTVM_NOTHING;
  OBJECT fileName = OBJECT_NOTVM_NOTHING;
  sbbox_t bbox ;
  int32 ssize ;

  static NAMETYPEMATCH ref_dictmatch[] = {
  /* 0 */ { NAME_F,                 3,  { OSTRING, ODICTIONARY, OINDIRECT }},
            DUMMY_END_MATCH
  } ;

  HQASSERT( pRefDict != NULL, "pRefDict null in pdf_Ref_dispatch" );
  HQASSERT( oType(*pRefDict) == ODICTIONARY, "/Ref object not a dictionary" );
  HQASSERT( pBBox != NULL, "pBBox null in pdf_Ref_dispatch" );

  *pWasRendered = FALSE;  /* Assume we've not rendered the PDF file */

  if (! pdfxReferenceXObjectDetected(pdfc))
    return FALSE ;

  /* Validate the Bounding Box and use it to construct a clipping
     rectangle. */
  if ( !object_get_bbox(pBBox, &bbox) )
    return FALSE ;

  bbox_to_rectangle(&bbox, &cliprect) ;

  /* Check out the Reference dictionary */
  if (!pdf_dictmatch( pdfc, pRefDict, ref_dictmatch ))
    return FALSE ;

  /* Parse the file specification */
  if (!pdf_Ref_getfile( pdfc, ref_dictmatch[0].result, &fileName, &pFileList ))
    return FALSE;

  if (pFileList == NULL) {
    /* File not found; this is not an error, but report the name of the missing
     * file. */
    if (oType(fileName) == OSTRING) {
      /* UVM("Failed to find external file: %s") */
      monitorf((uint8*)"Failed to find external file: %.*s\n",
               theLen(fileName), oString(fileName)) ;
    }
    return TRUE;
  }

  success = TRUE;

  /* If matrix not specified, default to an identity matrix */
  if (pMatrix == NULL) {
    if (!pdf_matrix( pdfc, &matrixArray ))
        return FALSE ;
    matrixAllocated = TRUE;
    pMatrix = &matrixArray;
  }

  /* Translate matrix array to an OMATRIX */
  if (!is_matrix( pMatrix, &matrix )) {
    success = error_handler(TYPECHECK);
  }  else {
    ps_context_t *pscontext ;

    /* Do a 'save'. Even though this is done for each PDF page, it needs to be
       done here due to name-caching problems on the catalog (et.al.) dictionary. */
    HQASSERT(pdfc->pdfxc != NULL, "No PDF execution context") ;
    HQASSERT(pdfc->pdfxc->corecontext != NULL, "No core context") ;
    pscontext = pdfc->pdfxc->corecontext->pscontext ;
    HQASSERT(pscontext != NULL, "No PostScript context") ;

    if ( ! save_(pscontext) )
      return FALSE ;

    ssize = theStackSize( operandstack );
    Copy( &save_obj, TopStack( operandstack, ssize ) );
    pop( &operandstack );


   /* Set the transformation matrix, and clip to the bounding box. */
    gs_modifyctm( &matrix );

    success = cliprectangles( &cliprect, 1 );
    if (success) {
      success = execExternalPage( pdfc, pFileList, pRefDict, pMetadata,
                                  pWasRendered );
    }

    /* Restore state */
    if (!push( &save_obj, &operandstack ) || ! restore_(pscontext))
      return FALSE;
  }

  if (matrixAllocated)              /* Destroy the matrix if one was allocated */
    pdf_destroy_array( pdfc, 6, pMatrix );

  return success;
}


/* Log stripped */
