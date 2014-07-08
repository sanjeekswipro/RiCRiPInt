/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfxobj.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF extended object (Xobj) handling.
 */

#include "core.h"
#include "swpdf.h"
#include "pdfxobj.h"

#include "swerrors.h"
#include "swmemory.h"
#include "objects.h"
#include "fileioh.h"
#include "dictscan.h"
#include "namedef_.h"

#include "control.h"
#include "bitblts.h"
#include "display.h"
#include "fileops.h"
#include "forms.h"
#include "graphics.h"
#include "matrix.h"
#include "stacks.h"
#include "pathops.h" /* transform_bbox */

#include "saves.h"    /* save_level */
#include "streamd.h"
#include "pdfmatch.h"
#include "pdffont.h"
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdfxref.h"

#include "pdfattrs.h"
#include "pdfexec.h"
#include "pdfimg.h"
#include "pdfops.h"
#include "pdfopt.h"
#include "pdfin.h"
#include "pdfdefs.h"
#include "pdfopi.h"
#include "pdfcolor.h"
#include "pdfrefs.h"
#include "pdfx.h"

#include "render.h"
#include "gstack.h"
#include "group.h"
#include "utils.h"

#include "routedev.h"
#include "execops.h"  /* setup_pending_exec */

/* Static functions */

static Bool pdfform_group_begin(PDFCONTEXT *pdfc ,
                                OBJECT *dict ,
                                OBJECT *source ,
                                Bool requireGroupColorSpace ,
                                SoftMaskType softmasktype ,
                                OBJECT *bgcolor,
                                OBJECT *xferfn,
                                Group **group,
                                int32 *gid,
                                uint32 *extractGroupId);
static Bool pdfform_group_end(Group **group, int32 gid, Bool result) ;
static Bool pdfform_dispatch( PDFCONTEXT *pdfc ,
                              OBJECT *dict ,
                              OBJECT *source ) ;
static Bool pdfps_dispatch( PDFCONTEXT *pdfc , OBJECT *dict , OBJECT *source ) ;

enum {e_common_type, e_common_subtype, e_common_oc, e_common_max};
static NAMETYPEMATCH pdfxobj_commondict[e_common_max + 1] = {
  { NAME_Type    | OOPTIONAL,   2, { ONAME, OINDIRECT }},
  { NAME_Subtype,               2, { ONAME, OINDIRECT }},
  { NAME_OC|OOPTIONAL,          2, { ODICTIONARY, OINDIRECT }},
  DUMMY_END_MATCH
} ;


/** \ingroup pdfops
 * \brief The PDF operator Do.
 *
 * This is a light wrapper for pdfop_DoInternal(); groups are not extracted.
 */
Bool pdfop_Do( PDFCONTEXT *pdfc )
{
  return pdf_DoExtractingGroup(pdfc, FALSE, EmptySoftMask,
                               NULL /*bgcolor*/, NULL /*xferfn*/,
                               NULL /*groupId*/);
}


/** \ingroup pdfops
 * \brief The PDF operator PS.
 *
 * This is not strictly an XObject, but it seems natural for it to live here.
 */
Bool pdfop_PS( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  OBJECT *theo ;
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;
  PDF_GET_IMC( imc ) ;

  if ( ! pdfxPSDetected( pdfc ))
    return FALSE ;

  HQTRACE( TRUE, ( "Untested: PS Operator." )) ;

  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop(*stack) ;

  if ( oType( *theo ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( !push(theo, &executionstack) )
    return FALSE ;

  if ( !interpreter(1, NULL) )
    return FALSE;

  pop( stack ) ;
  return TRUE ;
}


static Bool fetchalternateprintimage( PDFCONTEXT *pdfc , OBJECT * altlist, OBJECT ** p_stream )
{
  int32 len,i;
  OBJECT * arr;
  OBJECT * forprint;
  enum {e_image = 0, e_defaultforprinting, e_oc, e_alternates_dict_max};
  static NAMETYPEMATCH pdf_alternates_dict[e_alternates_dict_max + 1] = {
   { NAME_Image , 2, {OFILE, OINDIRECT}},
   { NAME_DefaultForPrinting | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
   { NAME_OC | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},

   DUMMY_END_MATCH
  } ;

  GET_PDFXC_AND_IXC;

  *p_stream = NULL;
  HQASSERT(altlist != NULL, "fetchalternateprintimage: altlist null");

  len = theLen(*altlist);
  arr = oArray(*altlist);

  /* For each alternate image XObject mentioned in the array */
  for (i = 0; i < len; i++, arr++) {
    if (oType(*arr) != ODICTIONARY)
      return error_handler(TYPECHECK);

    if (!pdf_dictmatch(pdfc, arr, pdf_alternates_dict))
      return FALSE;

    forprint = pdf_alternates_dict[e_defaultforprinting].result;
    if (forprint != NULL && oBool(*forprint)) {
      /* bingo! Now is it embedded and can we read it? */
      *p_stream = pdf_alternates_dict[e_image].result;
      break;
    }
  }

  return TRUE;
}

/** Perform the initial checks for invoking an XObject, returning the
    XObject subtype in \c *p_type. */

static Bool pdf_XObjectSubtype(PDFCONTEXT *pdfc, STACK **stack ,
                               OBJECT **res , OBJECT **dict ,
                               NAMECACHE **p_type)
{
  OBJECT *stackObject ;
  PDFXCONTEXT *pdfxc ;
  PDF_IMC_PARAMS *imc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  *p_type = NULL ;

  *stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( *stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  stackObject = theTop(**stack) ;

  /* Is the XObject provided directly, or as a named resource? */
  if ( oType( *stackObject ) == ONAME ) {
    /* Go and obtain the referred resource. */

    /* NB. We are not necessarily handling an image here, but we need
     *     to get the resource before we can decide whether it is an
     *     image. The only result of this is that other kinds of
     *     XObject could use non-compliant PDF/X-1 filters without
     *     warnings. So I'll leave it that.
     */
    imc->handlingImage = TRUE ;
    if ( ! pdf_get_resource( pdfc , NAME_XObject , stackObject , res ))
      return FALSE ;
    imc->handlingImage = FALSE ;
  }
  else {
    /* The xobject is an actual instance - no need to resolve it. */
    *res = stackObject ;
  }

  /* The resource object should be a stream */

  if ( oType( **res ) != OFILE )
    return error_handler( TYPECHECK ) ;

  /* Get the dictionary associated with the stream. */
  if ( NULL == ( *dict = streamLookupDict( *res )))
    return error_handler( UNDEFINED ) ;

  HQASSERT( oType( **dict ) == ODICTIONARY, "dict must be an ODICTIONARY." ) ;

  if ( ! pdf_dictmatch( pdfc , *dict , pdfxobj_commondict ))
    return FALSE ;

  /* Type should always be XObject when present (but since its optional and we
     don't use it anyway, put it under 'strict' flag control). */
  if ( pdfxobj_commondict[ e_common_type ].result != NULL && ixc->strictpdf )  {
    if ( oNameNumber(*(pdfxobj_commondict[ e_common_type ].result)) !=
         NAME_XObject ) {
      return error_handler( TYPECHECK ) ;
    }
  }

  *p_type = oName( *pdfxobj_commondict[ e_common_subtype ].result ) ;

  return TRUE ;
}

/** Simple test to determine whether the XObject or XObject resource
    reference on the stack is a Form. */

Bool pdf_XObjectIsForm(PDFCONTEXT *pdfc, Bool *is_form)
{
  STACK *stack ;
  OBJECT *res ;
  OBJECT *dict ;
  NAMECACHE *sub_type ;

  *is_form = FALSE ;

  if ( ! pdf_XObjectSubtype(pdfc, &stack, &res, &dict, &sub_type)) {
    return FALSE ;
  }

  *is_form = ( sub_type->namenumber == NAME_Form ) ;

  return TRUE ;
}

/** XObject invocation */

Bool pdf_DoExtractingGroup(PDFCONTEXT *pdfc,
                           Bool requireGroupColorSpace,
                           SoftMaskType softmasktype,
                           OBJECT *bgcolor,
                           OBJECT *xferfn,
                           uint32 *extractGroupId)
{
  STACK *stack ;
  OBJECT *res ;
  OBJECT *dict ;
  PDFXCONTEXT *pdfxc ;
  PDF_IMC_PARAMS *imc ;
  PDF_IXC_PARAMS *ixc ;
  NAMECACHE *sub_type ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  if ( ! pdf_XObjectSubtype(pdfc, &stack, &res, &dict, &sub_type)) {
    return FALSE ;
  }

  switch ( sub_type->namenumber ) {

  case NAME_Image: {
    OBJECT * id;
    enum {e_OC_notset, e_OC_ON, e_OC_OFF };
    int32 ocstate = e_OC_notset;
    Bool result ;

    enum { pi_alternate_Alternates, pi_alternate_dummy } ;
    static NAMETYPEMATCH pdfimg_alternate[pi_alternate_dummy + 1] = {
      { NAME_Alternates | OOPTIONAL,   3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},

      DUMMY_END_MATCH
    } ;

    HQASSERT(softmasktype == EmptySoftMask, "Soft mask cannot be an image");

    /* a marked content tag may have flagged this as ignored OC  */
    if (!optional_content_on)
      break;

    if (pdfxobj_commondict[ e_common_oc ].result != NULL) {
      Bool doit = TRUE;

      /* we want the indirect object*/
      if ( ! dictmatch( dict , pdfxobj_commondict ))
        return FALSE ;

      id = pdfxobj_commondict[ e_common_oc ].result;

      if (!pdf_oc_getstate_OCG_or_OCMD(pdfc, id, & doit)) {
        return FALSE;
      }

      ocstate = doit? e_OC_ON:e_OC_OFF;
    }

    if (ocstate != e_OC_ON)
    {
      /* Page 319 of the PDF 1.6 spec gives details of the precidence of
         alternative images. If OC is ON for the base image then that is
         used else the first valid printable image with OC ON is used
         else nothing is printed. The image may be superceeded by an
         alternate image even if OC is off*/
      if ( ! pdf_dictmatch( pdfc , dict , pdfimg_alternate ))
        return FALSE ;

      /* Perform checks on the Alternates first. */
      if ( pdfimg_alternate[pi_alternate_Alternates].result != NULL ) {
        OBJECT * altlist = pdfimg_alternate[pi_alternate_Alternates].result;
        OBJECT * altstream;
        OBJECT * altdict;

        if ( ! fetchalternateprintimage( pdfc, altlist, &altstream ) )
          return FALSE ;

        if ( altstream != NULL ) {
          enum { pi_alternateoc_OC, pi_alternateoc_dummy } ;
          static NAMETYPEMATCH pdfimg_alternateoc[pi_alternateoc_dummy + 1] = {
            { NAME_OC|OOPTIONAL,             2, { ODICTIONARY, OINDIRECT }},

            DUMMY_END_MATCH
          } ;

          /* we have a printable alternate image*/

          altdict = streamLookupDict(altstream);
          if (altdict == NULL)
            return error_handler(UNDEFINED);

          if ( ! pdf_dictmatch( pdfc , altdict , pdfimg_alternateoc ))
            return FALSE ;

          if (pdfimg_alternateoc[pi_alternateoc_OC].result != NULL ) {
            Bool doit = TRUE;

            id = pdfimg_alternateoc[pi_alternateoc_OC].result;

            if (!pdf_oc_getstate_OCG_or_OCMD(pdfc, id, & doit)) {
              return FALSE;
            }

            if ( !doit )
              break;
          }

          ocstate = e_OC_ON;
          dict = altdict;
          res = altstream;
        }
      }

      /* if OC is still OFF print nothing */
      if (ocstate == e_OC_OFF)
        break;

      /* otherwise stay with the base image for the case where no OC
         is defined and no suitable alternate image can be found */
    }

    imc->handlingImage = TRUE ;
    result = pdfimg_dispatch( pdfc , dict , res ) ;
    imc->handlingImage = FALSE ;
    if ( ! result )
      return FALSE ;
  }
    break;

  case NAME_Form:
    if (pdfxobj_commondict[ e_common_oc ].result != NULL) {
      Bool doit;
      OBJECT * id;

      if ( ! dictmatch( dict , pdfxobj_commondict ))
        return FALSE ;

      id = pdfxobj_commondict[ e_common_oc ].result;

      if (!pdf_oc_getstate_OCG_or_OCMD(pdfc, id, & doit)) {
        return FALSE;
      }

      if (!doit)
        break;
    }

    {
      Bool success = FALSE ;
      Group *group ;
      int32 gid ;
      Bool oc;

      /* preserve optional content global variable */
      oc = optional_content_on;

      /* The gs_execform() called by pdfform_dispatch will wrap this in
         GST_FORM saves.  */

      if ( pdfform_group_begin(pdfc, dict, res,
                               requireGroupColorSpace,
                               softmasktype,
                               bgcolor, xferfn,
                               &group, &gid, extractGroupId) ) {
        /* Perform the form dispatch */
        success = pdfform_dispatch(pdfc, dict, res) ;
        if ( !pdfform_group_end(&group, gid, success) )
          success = FALSE ;
      }

      optional_content_on = oc;

      if (! success)
        return FALSE;
    }
    break;

  case NAME_PS:
    if ( ! ixc->IgnorePSXObjects ) {
      HQTRACE( TRUE, ( "Untested: PS XObjects." )) ;
      if ( ! pdfps_dispatch( pdfc , dict , res ))
        return FALSE ;
    }
    break;

  default:
    return error_handler(RANGECHECK) ;
  }

  pop( stack ) ;
  return TRUE ;
}

/** If a form dictionary contains a 'Group' key, we need to wrap the form's
 * execution in a Group.
 * This function should probably be replaced/superceded by DIDL begin/end
 * calls on the Group target.
 */
static Bool pdfform_group_begin(PDFCONTEXT *pdfc,
                                OBJECT *dict,
                                OBJECT *source,
                                Bool requireGroupColorSpace,
                                SoftMaskType softmasktype,
                                OBJECT *bgcolor,
                                OBJECT *xferfn,
                                Group **group,
                                int32 *gid,
                                uint32 *extractGroupId)
{
  enum {
    form_match_Group, form_match_BBox, form_match_Matrix, form_match_dummy
  } ;
  static NAMETYPEMATCH form_match[form_match_dummy + 1] = {
    /* Index this match using the enum above */
    {NAME_Group | OOPTIONAL, 2, {ODICTIONARY, OINDIRECT}},
    {NAME_BBox, 3, {OARRAY, OPACKEDARRAY, OINDIRECT}},
    {NAME_Matrix | OOPTIONAL, 3, {OARRAY, OPACKEDARRAY, OINDIRECT}},
    DUMMY_END_MATCH
  };

  UNUSED_PARAM(OBJECT*, source);

  PDF_CHECK_MC(pdfc);

  *group = NULL ;
  *gid = GS_INVALID_GID ;

  HQASSERT(dict, "dict NULL preparing form.");
  HQASSERT(source, "source NULL preparing form.");
  HQASSERT(oType(*dict) == ODICTIONARY,
           "dict must be an ODICTIONARY.");
  HQASSERT(oType(*source) == OFILE,
           "source must be an OFILE.");

  /* Query the form dictionary for the keys we're interested in */
  if (! pdf_dictmatch(pdfc, dict, form_match))
    return FALSE;

  /* This form is a transparency group - create a new DLGroup. */
  if (form_match[form_match_Group].result != NULL) {
    Bool isolated = FALSE, knockout = FALSE;
    OBJECT colorSpace = OBJECT_NOTVM_NULL;
    OBJECT *groupDict = form_match[form_match_Group].result;
    GroupUsage groupusage;

    enum {
      group_match_S, group_match_CS, group_match_I, group_match_K,
      group_match_dummy
    } ;
    static NAMETYPEMATCH group_match[group_match_dummy + 1] = {
      /* Index this match using the enum above */
      {NAME_S, 2, {ONAME, OINDIRECT}},
      {NAME_CS, 4, {ONAME, OARRAY, OPACKEDARRAY, OINDIRECT}},
      {NAME_I | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
      {NAME_K | OOPTIONAL, 2, {OBOOLEAN, OINDIRECT}},
      DUMMY_END_MATCH
    };

    if (! requireGroupColorSpace)
      group_match[group_match_CS].name |= OOPTIONAL;
    else
      group_match[group_match_CS].name &= ~OOPTIONAL;

    /* Query the attributes dictionary for the keys we're interested in. */
    if (! pdf_dictmatch(pdfc, groupDict, group_match))
      return FALSE;

    /* PDF/X must be notified whenever transparency groups are detected. */
    if (oNameNumber(*group_match[group_match_S].result) == NAME_Transparency)
      if (! pdfxCheckTransparencyGroup(pdfc, groupDict))
        return FALSE;

    /* Read I (isolated) */
    if (group_match[group_match_I].result != NULL) {
      isolated = oBool(*group_match[group_match_I].result);
    }

    /* Only read the color space if required (note that isolated groups may
    specify a color space). */
    if (group_match[group_match_CS].result != NULL &&
        (requireGroupColorSpace || isolated)) {
      if (! pdf_mapBlendSpace(pdfc, *group_match[group_match_CS].result, &colorSpace))
        return FALSE;
    }

    /* Real K (knockout) */
    if (group_match[group_match_K].result != NULL) {
      knockout = oBool(*group_match[group_match_K].result);
    }

    /* Open the group. */
    switch (softmasktype) {
    default: HQFAIL("Unexpected softmasktype");
    case EmptySoftMask:
      groupusage = GroupSubGroup;
      break;
    case AlphaSoftMask:
      groupusage = GroupAlphaSoftMask;
      isolated = TRUE ;
      break;
    case LuminositySoftMask:
      groupusage = GroupLuminositySoftMask;
      isolated = TRUE ;
      break;
    }

    if ( gs_gpush(GST_GROUP) ) {
      int32 groupgid = gstackptr->gId;

      if (groupOpen(pdfc->corecontext->page, colorSpace, isolated, knockout,
                    TRUE /*Banded*/, bgcolor, xferfn, NULL /*patternTA*/,
                    groupusage, group)) {
        if ( gs_gpush(GST_GSAVE) ) {
          if ( extractGroupId )
            *extractGroupId = groupId(*group) ;
          *gid = groupgid ;
          return TRUE ;
        }
        (void)groupClose(group, FALSE) ;
      }
      (void)gs_cleargstates(groupgid, GST_GROUP, NULL) ;
    }

    return FALSE ;
  }
  else {
    /* This form is not a transparency group - were we expecting one? */
    if (extractGroupId != NULL) {
      HQFAIL("pdfform_predispatch() - expected group not present.");
      return error_handler(UNREGISTERED);
    }
  }

  return TRUE ;
}

static Bool pdfform_group_end(Group **group, int32 gid, Bool result)
{
  /* Close the Group if it was opened. */
  if (*group != NULL && !groupClose(group, result))
    result = FALSE;

  if ( gid != GS_INVALID_GID && !gs_cleargstates(gid, GST_GROUP, NULL) )
    result = FALSE;

  return result ;
}

/** Allocate an extra entry for the local dictionary in pdfform_dispatch.
   gs_execform does an insert_hash (with the key /Implementation). */
#define PDF_FORM_DICT_LEN 5

extern OBJECT internaldict ;

/** Dispatch a form. If it really is a form, all I have to do is
 * insert the source stream as the PaintProc and dispatch it to
 * gs_execform. If it's another XObject masquerading as a form,
 * just redirect as necessary. As with images, now a local copy
 * of the dictionary is dispatched.
 */

static Bool pdfform_dispatch( PDFCONTEXT *pdfc , OBJECT *dict , OBJECT *source )
{
  enum {
    fm_BBox, fm_FormType, fm_Matrix, fm_Resources, fm_XUID, fm_OPI,
    fm_Subtype2, fm_PS, fm_Ref, fm_Metadata,
    fm_dummy
  } ;
  static NAMETYPEMATCH form_dictmatch[fm_dummy + 1] = {
  /* x    { NAME_Type,                   1,  { ONAME }}, */
  /* x    { NAME_Subtype,                1,  { ONAME }}, */
    { NAME_BBox,                   3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_FormType | OOPTIONAL,   2,  { OINTEGER, OINDIRECT }},
    { NAME_Matrix | OOPTIONAL,     3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
  /* x    { NAME_Name,                   1,  { ONAME }}, */
    { NAME_Resources | OOPTIONAL,  2,  { ODICTIONARY, OINDIRECT }},
    { NAME_XUID | OOPTIONAL,       3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_OPI | OOPTIONAL,        2,  { ODICTIONARY, OINDIRECT }},
    { NAME_Subtype2 | OOPTIONAL,   2,  { ONAME, OINDIRECT }},
    { NAME_PS | OOPTIONAL,         2,  { OFILE, OINDIRECT }},
    { NAME_Ref | OOPTIONAL,        2,  { ODICTIONARY, OINDIRECT }},
    { NAME_Metadata | OOPTIONAL,   2,  { OFILE, OINDIRECT }},
    DUMMY_END_MATCH
  } ;
  enum {
    fr_Fonts, fr_dummy
  } ;
  static NAMETYPEMATCH res_dictmatch[fr_dummy + 1] = {
    { NAME_Font | OOPTIONAL,       2,  { ODICTIONARY, OINDIRECT }},
    DUMMY_END_MATCH
  } ;
  int32 len = PDF_FORM_DICT_LEN ;
  Bool matrixAllocated = FALSE;
  Bool do_restore = FALSE;
  OBJECT nameobj = OBJECT_NOTVM_NOTHING ;
  OBJECT matrix = OBJECT_NOTVM_NOTHING ;
  OBJECT formType = OBJECT_NOTVM_NOTHING ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  HQASSERT( dict , "dict NULL in pdfform_dispatch." ) ;
  HQASSERT( source , "source NULL in pdfform_dispatch." ) ;
  HQASSERT( oType( *dict ) == ODICTIONARY,
            "dict must be an ODICTIONARY." ) ;
  HQASSERT( oType( *source ) == OFILE,
            "source must be an OFILE." ) ;

  if ( ! pdf_dictmatch( pdfc , dict , form_dictmatch ))
    return FALSE ;

  /* If SubType2 is present, we have a different XObject pretending
   * to be a form for the sake of PDF 1.0 compatibility. The only
   * kind of such objects supported in PDF 1.2 is PS.
   */

  if ( form_dictmatch[fm_Subtype2].result ) {
    if ( oNameNumber(*(form_dictmatch[fm_Subtype2].result)) == NAME_PS ) {
      if ( form_dictmatch[fm_PS].result )
        return pdfps_dispatch(pdfc, dict, form_dictmatch[fm_PS].result) ;

      HQFAIL( "Subtype2 is PS but there's no PS key in pdfform_dispatch." ) ;
      return error_handler(UNDEFINED) ;
    }

    HQFAIL( "Unrecognised PDF 1.0 compatible Subtype2 in pdfform_dispatch." ) ;
    return error_handler(RANGECHECK) ;
  }
  else {
    OBJECT localdict = OBJECT_NOTVM_NOTHING ;
    ps_context_t *pscontext ;

    HQASSERT(pdfc->pdfxc != NULL, "No PDF execution context") ;
    HQASSERT(pdfc->pdfxc->corecontext != NULL, "No core context") ;
    pscontext = pdfc->pdfxc->corecontext->pscontext ;
    HQASSERT(pscontext != NULL, "No PostScript context") ;

    if ( form_dictmatch[fm_OPI].result ) {
      int32 rendered ;

      if ( ! pdfOPI_dispatch(pdfc, form_dictmatch[fm_OPI].result, &rendered) )
        return FALSE ;

      if ( rendered )
        return TRUE ;
    }

    if ( form_dictmatch[fm_Ref].result ) {   /* /Ref */
      Bool rendered ;

      if ( ! pdf_Ref_dispatch( pdfc,
                               form_dictmatch[fm_Ref].result,
                               form_dictmatch[fm_BBox].result,
                               form_dictmatch[fm_Matrix].result,
                               form_dictmatch[fm_Metadata].result,
                               &rendered ))
        return FALSE ;

      if ( rendered )
        return TRUE ;
    }

    /* Make a local dictionary, populate it, and dispatch it.
     * The Resources key isn't needed by gs_execform itself, but
     * by pdf_exec_stream.
     */

    if ( form_dictmatch[fm_Resources].result )
      len++ ;

    /* If there's a Font resource, bracket the form with save/restore and
     * reset the standard fonts. Note that we must do the save_() BEFORE
     * pdf_create_dictionary().  [12448]
     */
    if ( form_dictmatch[fm_Resources].result &&
         pdf_dictmatch( pdfc , form_dictmatch[fm_Resources].result ,
                        res_dictmatch ) &&
         res_dictmatch[fr_Fonts].result ) {

      OBJECT* resetstandardfonts ;
      resetstandardfonts =
        fast_extract_hash_name( &internaldict, NAME_resetstandardfonts ) ;

      do_restore = TRUE ;

      if ( resetstandardfonts == NULL ||
           !save_(pscontext) ||
           !setup_pending_exec(resetstandardfonts,TRUE) )
        return FALSE ;
    }

    if ( !pdf_create_dictionary( pdfc , len , & localdict ))
      return FALSE ;

    object_store_name(&nameobj, NAME_FormType, LITERAL) ;

    /* The FormType parameter was optional, but execform requires it */
    if ( form_dictmatch[fm_FormType].result != NULL ) {
      if ( !pdf_fast_insert_hash( pdfc , & localdict , & nameobj ,
            form_dictmatch[fm_FormType].result ))
        return FALSE ;
    }
    else {
      /* If the FormType was absent, use a default of 1 */
      object_store_integer(&formType, 1) ;
      if (!pdf_fast_insert_hash(pdfc, &localdict, &nameobj, &formType))
        return FALSE ;
    }

    object_store_name(&nameobj, NAME_BBox, LITERAL) ;

    if ( !pdf_fast_insert_hash( pdfc , & localdict , & nameobj ,
          form_dictmatch[fm_BBox].result ))
      return FALSE ;

    /* The matrix parameter was optional, but execform requires it */
    object_store_name(&nameobj, NAME_Matrix, LITERAL) ;
    if ( form_dictmatch[fm_Matrix].result != NULL ) {
      if ( !pdf_fast_insert_hash( pdfc , & localdict , & nameobj ,
                                  form_dictmatch[fm_Matrix].result ))
        return FALSE ;
    }
    else {
      /* If the matrix was not present, use the identity matrix */
      if ( !pdf_matrix(pdfc, &matrix) ||
           !pdf_fast_insert_hash(pdfc, &localdict, &nameobj, &matrix) )
        return FALSE ;
      matrixAllocated = TRUE;
    }

    object_store_name(&nameobj, NAME_PaintProc, LITERAL) ;

    if ( !pdf_fast_insert_hash( pdfc , & localdict , & nameobj , source ))
      return FALSE ;

    /* Insert Resources AFTER the resolvexrefs - the resources
     * machinery doesn't work when resource dictionaries are
     * inlined behind its back.
     */

    if ( form_dictmatch[fm_Resources].result ) {
      object_store_name(&nameobj, NAME_Resources, LITERAL);

      if ( !pdf_fast_insert_hash( pdfc , & localdict , & nameobj ,
            form_dictmatch[fm_Resources].result ))
        return FALSE ;
    }

    if ( ! push( & localdict , & imc->pdfstack ))
      return FALSE ;

    if ( ! gs_execform( pdfc->corecontext, & imc->pdfstack ))
      return FALSE ;

    pop( & imc->pdfstack ) ; /* Need to pop this, since gs_execform doesn't. */

    /* Destroy the matrix if we created one */
    if (matrixAllocated) {
      pdf_destroy_array(pdfc, 6, &matrix);
    }

    pdf_destroy_dictionary( pdfc , len , & localdict ) ;

    if ( do_restore && !restore_(pscontext) )
      return FALSE ;
  }

  return TRUE ;
}

static Bool pdfps_dispatch(PDFCONTEXT *pdfc, OBJECT *dict, OBJECT *source)
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  UNUSED_PARAM( OBJECT * , dict ) ;

  HQASSERT( source , "source NULL in pdfps_dispatch." ) ;
  HQASSERT( oType( *source ) == OFILE, "source must be an OFILE." ) ;

  if ( ! pdfxPSXObjectDetected( pdfc ))
    return FALSE ;

  /* No need to do a dictmatch since I don't care about any Level1 key. */

  currfileCache = NULL ;
  if ( ! push( source , & executionstack ) ||
       ! interpreter( 1 , NULL ))
    return FALSE ;

  return TRUE ;
}

/* Log stripped */
