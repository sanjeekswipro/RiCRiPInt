/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfgs4.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Gstate interface for transparency state (PDF 1.4).
 */

#include "core.h"
#include "pdfgs4.h"

#include "objects.h"
#include "namedef_.h"
#include "swerrors.h"
#include "gstate.h"
#include "stream.h"
#include "gschead.h"
#include "tranState.h"
#include "display.h"
#include "swmemory.h" /* gs_cleargstates */
#include "images.h"

#include "pdfin.h"
#include "pdfmatch.h"
#include "pdfxref.h"
#include "pdfmem.h"
#include "pdfgstat.h"
#include "pdfcolor.h"
#include "pdfxobj.h"
#include "dlstate.h" /* inputpage */
#include "execops.h"
#include "pathops.h" /* transform_bbox */
#include "params.h"
#include "rectops.h"
#include "routedev.h"
#include "plotops.h"
#include "gu_ctm.h" /* gs_setctm */
#include "group.h"  /* groupId */
#include "pdfinmetrics.h"
#include "gscdevci.h"  /* gsc_disableOverprint */

/* --Private Prototypes-- */

static Bool lookupGroup(PDFCONTEXT* pdfc,
                        OBJECT groupStream,
                        OBJECT* xObjDict,
                        OBJECT* groupAttribDict);
static Bool validateSoftMaskDict(PDFCONTEXT* pdfc,
                                 OBJECT dictionary,
                                 OBJECT* groupDictionary);

/* --Private constants-- */

/* Access these match entries using the enumerated values below. */
enum { smask_Type, smask_S, smask_G, smask_BC, smask_TR, smask_dummy } ;
static NAMETYPEMATCH smaskDictmatch[smask_dummy + 1] = {
  {NAME_Type | OOPTIONAL, 2, {ONAME, OINDIRECT}},
  {NAME_S, 2, {ONAME, OINDIRECT}},
  {NAME_G, 2, {OFILE, OINDIRECT}},
  {NAME_BC | OOPTIONAL, 3, {OARRAY, OPACKEDARRAY, OINDIRECT}},
  {NAME_TR | OOPTIONAL, 6, {ONAME, ODICTIONARY, OFILE, OARRAY,
                            OPACKEDARRAY, OINDIRECT}},
  DUMMY_END_MATCH
};

/* --Public methods-- */

/* Set the current blend mode. This can be a single name (e.g. Normal), or an
 * array of names. The names are not validated - we don't really know what
 * blend modes are supported at this stage.
 */
Bool pdf_setBlendMode(PDFCONTEXT* pdfc, OBJECT object)
{
  PDF_CHECK_MC(pdfc);

  /* Resolve any indirect references */
  if (!pdf_resolvexrefs(pdfc, &object)) {
    return FAILURE(FALSE) ;
  }

  /* Validate the type of the parameter - it must be a name or name array */
  switch (oType(object)) {

    case OARRAY:
    case OPACKEDARRAY:

      if (theLen(object) <= 0) {
        return error_handler(RANGECHECK) ;
      }
      else {
        int32 i;
        OBJECT *list = oArray(object);

        /* Ensure that each object in the array is a name */
        for (i = 0; i < theLen(object); i ++) {
          if (oType(list[i]) != ONAME) {
            return error_handler(TYPECHECK) ;
          }
        }
      }
      break;

    case ONAME:
      /* A name is ok */
      break;

    default:
      /* Any other type is unacceptable */
      return error_handler(TYPECHECK) ;
  }

  tsSetBlendMode(gsTranState(gstateptr), object, gstateptr->colorInfo);
  return TRUE;
}

/* Set the soft mask. This must be either 'None', or a softmask dictionary
 * containing a group object to use for the mask.
 */
Bool pdf_setSoftMask(PDFCONTEXT* pdfc, OBJECT object)
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC(pdfc);
  PDF_GET_IMC(imc);

  /* Resolve any indirect references */
  if (!pdf_resolvexrefs(pdfc, &object))
    return FAILURE(FALSE) ;

  switch (oType(object)) {

  case ONAME:
    if (oNameNumber(object) != NAME_None)
      return error_handler(UNDEFINED) ;

    /* Set an empty softmask */
    if (! tsSetSoftMask(gsTranState(gstateptr), EmptySoftMask,
                        HDL_ID_INVALID, gstateptr->colorInfo))
      return FAILURE(FALSE) ;
    break;

  case ODICTIONARY: {
    Bool requireColorSpace = FALSE;
    SoftMaskType type = AlphaSoftMask;
    OBJECT groupXObjDictionary = OBJECT_NOTVM_NOTHING;

    /* Validate the dictionary - this will ensure that all relevant values
     * are present (it will add defaults for any absentees). */
    if (! validateSoftMaskDict(pdfc, object, &groupXObjDictionary))
      return FAILURE(FALSE) ;

    /* Extract all the relevant values from the dictionary. */
    if (! pdf_dictmatch(pdfc, &object, smaskDictmatch))
      return FAILURE(FALSE) ;

    /* Determine the type. */
    if (oNameNumber(*smaskDictmatch[smask_S].result) == NAME_Luminosity) {
      type = LuminositySoftMask;
      requireColorSpace = TRUE;
    }

#if defined( METRICS_BUILD )
    if (type == AlphaSoftMask)
      pdfin_metrics.softMaskCounts.alpha ++;
    else
      pdfin_metrics.softMaskCounts.luminosity ++;
#endif

    /* Dispatch the form to obtain the Group. */
    if (! push(smaskDictmatch[smask_G].result, &imc->pdfstack))
      return FAILURE(FALSE) ;

    {
      uint32 groupid = HDL_ID_INVALID;

      if (! pdf_DoExtractingGroup(pdfc, requireColorSpace, type,
                                  smaskDictmatch[smask_BC].result,
                                  smaskDictmatch[smask_TR].result,
                                  &groupid) ||
          !tsSetSoftMask(gsTranState(gstateptr), type,
                         groupid,
                         gstateptr->colorInfo) )
        return FAILURE(FALSE) ;
    }

    break;
  }

  default:
    return error_handler(TYPECHECK) ;
  }

  return TRUE;
}

/* Set the passed DeviceGray image (which should be a PS style image
 * dictionary) as a soft mask.
 */
Bool pdf_setGrayImageAsSoftMask(PDFCONTEXT *pdfc, OBJECT image)
{
  Group *group = NULL ;
  Bool result = FALSE ;

  PDF_CHECK_MC(pdfc);

#define return DO_NOT_RETURN_-_SET_result_INSTEAD!
  /* We need to save the current gstate before we install the DeviceGray color
     space. */
  if ( gs_gpush(GST_GROUP) ) {
    int32 gid = gstackptr->gId ;

    /* Set the colorspace to device gray and initialise the constant alpha. */
    if ( gsc_setcolorspacedirect(gstateptr->colorInfo, GSC_FILL, SPACE_DeviceGray)) {
      OBJECT gray = OBJECT_NOTVM_NAME(NAME_DeviceGray, LITERAL) ;

      tsSetConstantAlpha(gsTranState(gstateptr), FALSE, 1, gstateptr->colorInfo);

      /* Open a group to capture the image. */
      if ( groupOpen(pdfc->corecontext->page, gray, TRUE /*I*/, FALSE /*K*/,
                     TRUE /*Banded*/, NULL /*bgcolor*/, NULL /*xferfn*/,
                     NULL /*patternTA*/, GroupLuminositySoftMask, &group)) {
        if ( gs_gpush(GST_GSAVE) ) {
          OBJECT psDict = OBJECT_NOTVM_NOTHING;

          /* Copy the pdf dictionary into PostScript memory as we're calling a
             PostScript function (we don't want to risk having a mixed pdf/ps
             memory dictionary). */
          if ( pdf_copyobject(NULL, &image, &psDict)) {
            PDF_IMC_PARAMS* imc;

            /* Get hold of the PDF stack. */
            PDF_GET_IMC(imc);

            /* Draw the image. */
            if ( push(&psDict, &imc->pdfstack) ) {
              if ( gs_image(pdfc->corecontext, &imc->pdfstack)) {
                result = TRUE ;
              } else {
                pop(&imc->pdfstack) ;
              }
            }
          } else /* Some routes through pdf_copyobject don't set errors. */
            result = error_handler(VMERROR);
        }

        /* Close the group. */
        if ( !groupClose(&group, result) )
          result = FALSE ;
      }
    }

    /* Restore gstate. */
    if ( !gs_cleargstates(gid, GST_GROUP, NULL) )
      result = FALSE;

    /* Install the soft mask. */
    if ( result )
      result = tsSetSoftMask(gsTranState(gstateptr), LuminositySoftMask,
                             groupId(group), gstateptr->colorInfo);
  }

#undef return
  return result;
}

/* Validate a group attributes dictionary
 */
static Bool pdf_validateGroupAttributes(PDFCONTEXT* pdfc,
                                        OBJECT attrib,
                                        Bool requireColorSpace,
                                        int32* colorspaceDimension)
{
  OBJECT *object;
  enum { ad_Type, ad_S, ad_CS, ad_I, ad_K, ad_dummy } ;
  static NAMETYPEMATCH attribDictmatch[ad_dummy + 1] = {
    { NAME_Type | OOPTIONAL, 2, {ONAME, OINDIRECT}},
    { NAME_S, 2, {ONAME, OINDIRECT}},
    { NAME_CS | OOPTIONAL, 4, {ONAME, OARRAY, OPACKEDARRAY, OINDIRECT}},
    { NAME_I | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
    { NAME_K | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
    DUMMY_END_MATCH
  };

  PDF_CHECK_MC(pdfc);
  HQASSERT(oType(attrib) == ODICTIONARY,
           "pdf_validateGroupAttributes - attrib is not a dictionary");

  /* Remove the optional flag on the colorspace if we require it */
  if (requireColorSpace)
    attribDictmatch[ad_CS].name &= ~OOPTIONAL;
  else
    attribDictmatch[ad_CS].name |= OOPTIONAL;

  if (! pdf_dictmatch(pdfc, &attrib, attribDictmatch))
    return FAILURE(FALSE) ;

  /* Type - must be Group */
  object = attribDictmatch[ad_Type].result;
  if (object != NULL &&
      oNameNumber(*object) != NAME_Group)
    return error_handler(UNDEFINED) ;

  /* Subtype (S). Must be Transparency */
  object = attribDictmatch[ad_S].result;
  if (object != NULL &&
      oNameNumber(*object) != NAME_Transparency)
    return error_handler(UNDEFINED) ;

  /* Group color space (CS) */
  object = attribDictmatch[ad_CS].result;
  if (object != NULL) {
    OBJECT mappedColorSpace = OBJECT_NOTVM_NOTHING;

    if (! pdf_mapBlendSpace(pdfc, *object, &mappedColorSpace))
      return FAILURE(FALSE) ;

    if (colorspaceDimension != NULL) {
      COLORSPACE_ID colorspaceid;
      if (! gsc_getcolorspacesizeandtype(gstateptr->colorInfo, &mappedColorSpace,
                                         &colorspaceid, colorspaceDimension))
        return FAILURE(FALSE) ;
    }

    if (! compare_objects(object, &mappedColorSpace)) {
      OBJECT colorSpace = OBJECT_NOTVM_NOTHING;
      OBJECT colorSpaceName = OBJECT_NOTVM_NAME(NAME_CS, LITERAL);

      /* Insert the mapped color space back into the group attributes
       * dictionary.
       * Take a copy of the mapped object to prevent problems when freeing the
       * memory allocated for the attributes dictionary. The new object must be
       * unique (i.e. Not stored in the XREF cache) and allocated from the PDF
       * object pool since it is inserted back into the group attributes
       * dictionary.
       */
      if (!pdf_copyobject(pdfc, &mappedColorSpace, &colorSpace) ||
          !pdf_fast_insert_hash(pdfc, &attrib, &colorSpaceName, &colorSpace)) {
        return FAILURE(FALSE) ;
      }
    }
  }
  return TRUE;
}

/* --Private methods-- */

/* Lookup the group XObject dictionary, and group attributes dictionary from the
passed group stream, returning them via the 'xObjDict' and 'groupAttribDict'
parameters. Returns FALSE if an error occured.
*/
static Bool lookupGroup(PDFCONTEXT* pdfc,
                        OBJECT groupStream,
                        OBJECT* xObjDict,
                        OBJECT* groupAttribDict)

{
  FILELIST* file;
  OBJECT* streamDict;
  enum { gd_Group, gd_dummy } ;
  static NAMETYPEMATCH groupDictmatch[gd_dummy + 1] = {
    {NAME_Group, 2, {ODICTIONARY, OINDIRECT}},
    DUMMY_END_MATCH
  };

  PDF_CHECK_MC(pdfc);
  HQASSERT(oType(groupStream) == OFILE,
           "lookupGroup - groupStream is not a file");
  HQASSERT(xObjDict != NULL && groupAttribDict != NULL,
           "lookupGroup - parameters cannot be NULL");

  /* Ensure the group object stream is accessable */
  file = oFile(groupStream);
  if (!isIInputFile(file) ||
      !isIOpenFileFilter(&groupStream, file) ||
      !isIRewindable(file)) {
    return error_handler(IOERROR) ;
  }

  streamDict = streamLookupDict(&groupStream);
  HQASSERT(streamDict != NULL, "streamDict is missing");
  if (streamDict == NULL)
    return error_handler(UNDEFINED) ;

  /* All objects in a stream dictionary are supposed to be direct, but
   * we'll allow for indirect just to be safe. */
  if (! pdf_dictmatch(pdfc, streamDict, groupDictmatch))
    return FAILURE(FALSE) ;

  *xObjDict = *streamDict;
  *groupAttribDict = *groupDictmatch[gd_Group].result;
  return TRUE;
}

/* Validate the soft mask dictionary. The group dictionary is looked-up as part
of this validation - 'groupXObjDictionary', if not null, will be set to this
dictionary.
*/
static Bool validateSoftMaskDict(PDFCONTEXT* pdfc,
                                 OBJECT dictionary,
                                 OBJECT* groupXObjDictionary)
{
  Bool luminosity = FALSE;
  OBJECT* object;
  OBJECT groupXObj = OBJECT_NOTVM_NOTHING;
  OBJECT groupAttribs = OBJECT_NOTVM_NOTHING;
  int32 colorspaceDimension = 0;

  PDF_CHECK_MC(pdfc);
  HQASSERT(oType(dictionary) == ODICTIONARY,
           "validateSoftMaskDict - 'dictionary' is not a dictionary object");
  HQASSERT(groupXObjDictionary != NULL, "groupXObjDictionary is NULL");

  if (! pdf_dictmatch(pdfc, &dictionary, smaskDictmatch))
    return FAILURE(FALSE) ;

  /* Type must be 'Mask' if present */
  object = smaskDictmatch[smask_Type].result;
  if (object != NULL && oNameNumber(*object) != NAME_Mask)
    return error_handler(UNDEFINED) ;

  /* Subtype must be either 'Alpha' or 'Luminosity' */
  object = smaskDictmatch[smask_S].result;
  switch (oNameNumber(*object)) {
  case NAME_Luminosity:
    luminosity = TRUE;
    break;

  case NAME_Alpha:
    luminosity = FALSE;
    break;

  default:
    return error_handler(RANGECHECK) ;
  }

  /* Transparency group XObject - lookup and validate */
  if (! lookupGroup(pdfc, *smaskDictmatch[smask_G].result, &groupXObj,
                    &groupAttribs))
    return FAILURE(FALSE) ;

  *groupXObjDictionary = groupXObj;

  if (! pdf_validateGroupAttributes(pdfc, groupAttribs, luminosity,
                                    & colorspaceDimension))
    return FAILURE(FALSE) ;

  /* 'TR' (Transfer function) */
  if (smaskDictmatch[smask_TR].result != NULL) {
    /* Map the function. */
    if (! pdf_map_function(pdfc, smaskDictmatch[smask_TR].result))
      return FAILURE(FALSE) ;
  }

  /* Backdrop Color must be array of numbers with array length
     matching the dimension of the group color space */
  if (luminosity && smaskDictmatch[smask_BC].result != NULL) {
    int32 i;
    HQASSERT(colorspaceDimension >= 1 && colorspaceDimension <= 4,
             "dimension of a blend space expected to be 1 to 4");
    object = smaskDictmatch[smask_BC].result;
    if (theLen(*object) != colorspaceDimension)
      return error_handler(RANGECHECK) ;
    object = oArray(*object);
    for (i = 0; i < colorspaceDimension; ++i) {
      if (oType(object[i]) != OINTEGER && oType(object[i]) != OREAL)
        return error_handler(TYPECHECK) ;
    }
  }

  return pdf_resolvexrefs(pdfc, &dictionary);
}

/* Log stripped */
