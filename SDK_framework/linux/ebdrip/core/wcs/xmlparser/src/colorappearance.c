/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:colorappearance.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorAppearanceModel callbacks
 */

#include "core.h"
#include "wcsxml.h"
#include "xmlgattrs.h"
#include "xmltypeconv.h"
#include "namedef_.h"
#include "swerrors.h"

static Bool wcs_get_cam(xmlGFilter *filter, wcsColorAppearanceModel **cam)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  if ( !xml_doc )
    return error_handler(UNDEFINED) ;

  *cam = xml_doc->model.cam ;

  return TRUE ;
}

void wcs_destroy_cam(sw_memory_instance *memstate, wcsColorAppearanceModel *cam)
{
  if ( cam ) {
    if ( cam->header.profilename )
      wcscmm_free(memstate, cam->header.profilename) ;
    if ( cam->header.description )
      wcscmm_free(memstate, cam->header.description) ;
    if ( cam->header.author )
      wcscmm_free(memstate, cam->header.author) ;

    if ( cam->id )
      wcscmm_free(memstate, cam->id) ;

    wcscmm_free(memstate, cam) ;
  }
}

static Bool wcs_ColorAppearanceModel_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  static const wcsColorAppearanceModel init_cam = { 0,0,0, 0,0,0, 0,0,TRUE } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ProfileName), XML_INTERN(ns_2005_02_wcs_cam), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(Description), XML_INTERN(ns_2005_02_wcs_cam), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Author), XML_INTERN(ns_2005_02_wcs_cam), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(ViewingConditions), XML_INTERN(ns_2005_02_wcs_cam), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(NormalizeToMediaWhitePoint), XML_INTERN(ns_2005_02_wcs_cam), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  static Bool id_set ;
  static char *id ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(ID), NULL, &id_set, wcs_convert_string /** \todo May not be the right converter for 'ID'. */, &id},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if ( xml_doc->modeltype != wcsColorAppearanceModelType || xml_doc->model.cam )
    return error_handler(UNDEFINED) ;

  xml_doc->model.cam = wcscmm_alloc(xml_doc->memstate, sizeof(wcsColorAppearanceModel)) ;
  if ( !xml_doc->model.cam )
    return error_handler(VMERROR) ;

  *xml_doc->model.cam = init_cam ;
  xml_doc->model.cam->id = id_set ? id : NULL ;

  return TRUE ;
}

static Bool wcs_ColorAppearanceModel_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  wcsColorAppearanceModel *cam ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !wcs_get_cam(filter, &cam) )
    success = FALSE ;

  if ( !success && cam ) {
    xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
    wcs_destroy_cam(xml_doc->memstate, cam) ;
    xml_doc->model.cam = NULL ;
  }

  return success ;
}

static Bool wcs_ProfileName_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cam(filter, &cam) )
    return FALSE ;

  xml_doc->textptr = &cam->header.profilename ;

  return TRUE ;
}

static Bool wcs_Description_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cam(filter, &cam) )
    return FALSE ;

  xml_doc->textptr = &cam->header.description ;

  return TRUE ;
}

static Bool wcs_Author_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cam(filter, &cam) )
    return FALSE ;

  xml_doc->textptr = &cam->header.author ;

  return TRUE ;
}

static Bool wcs_ViewingConditions_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(WhitePoint), XML_INTERN(ns_2005_02_wcs_cam), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(WhitePointName), XML_INTERN(ns_2005_02_wcs_cam), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(Background), XML_INTERN(ns_2005_02_wcs_cam), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(ImpactOfSurround), XML_INTERN(ns_2005_02_wcs_cam), XMLG_GROUP_ONE_OF, 2},
    { XML_INTERN(Surround), XML_INTERN(ns_2005_02_wcs_cam), XMLG_GROUP_ONE_OF, 2},
    { XML_INTERN(LuminanceOfAdaptingField), XML_INTERN(ns_2005_02_wcs_cam), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(DegreeOfAdaptation), XML_INTERN(ns_2005_02_wcs_cam), XMLG_G_SEQUENCED_ONE, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

static Bool wcs_WhitePoint_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorAppearanceModel *cam ;

  static wcsNonNegCIEXYZ cie ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(X), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[0]},
    { XML_INTERN(Y), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[1]},
    { XML_INTERN(Z), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[2]},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cam(filter, &cam) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  cam->whitepoint = cie ;

  return TRUE ;
}

static Bool wcs_WhitePointName_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cam(filter, &cam) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_name(filter, NULL, &scan, &cam->whitepointname) )
    goto cleanup ;

  switch ( cam->whitepointname ) {
  case WCS_NAME_D50:
  case WCS_NAME_D65:
  case WCS_NAME_A:
  case WCS_NAME_F2:
    break ;
  default:
    (void)error_handler(RANGECHECK) ;
    goto cleanup ;
  }

  success = TRUE ;
 cleanup :
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return success ;
}

static Bool wcs_Background_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorAppearanceModel *cam ;

  static wcsNonNegCIEXYZ cie ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(X), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[0]},
    { XML_INTERN(Y), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[1]},
    { XML_INTERN(Z), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[2]},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cam(filter, &cam) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  cam->background = cie ;

  return TRUE ;
}

static Bool wcs_ImpactOfSurround_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cam(filter, &cam) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_float(filter, NULL, &scan, &cam->impactofsurround) )
    goto cleanup ;

  if ( cam->impactofsurround < 0.525 || cam->impactofsurround > 0.69 ) {
    (void)error_handler(RANGECHECK) ;
    goto cleanup ;
  }

  success = TRUE ;
 cleanup :
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return success ;
}

static Bool wcs_Surround_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cam(filter, &cam) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_name(filter, NULL, &scan, &cam->surround) )
    goto cleanup ;

  switch ( cam->surround ) {
  case WCS_NAME_Average:
  case WCS_NAME_Dim:
  case WCS_NAME_Dark:
    break ;
  default:
    (void)error_handler(RANGECHECK) ;
    goto cleanup ;
  }

  success = TRUE ;
 cleanup :
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return success ;
}

static Bool wcs_LuminanceOfAdaptingField_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cam(filter, &cam) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_float(filter, NULL, &scan, &cam->luminance) )
    goto cleanup ;

  if ( cam->luminance > 10000 ) {
    (void)error_handler(RANGECHECK) ;
    goto cleanup ;
  }

  success = TRUE ;
 cleanup :
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return success ;
}

static Bool wcs_DegreeOfAdaptation_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cam(filter, &cam) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_float(filter, NULL, &scan, &cam->degreeofadaptation) )
    goto cleanup ;

  if ( cam->degreeofadaptation < 0.0 || cam->degreeofadaptation > 1.0 ) {
    (void)error_handler(RANGECHECK) ;
    goto cleanup ;
  }

  success = TRUE ;
 cleanup :
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return success ;
}

static Bool wcs_NormalizeToMediaWhitePoint_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorAppearanceModel *cam ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cam(filter, &cam) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_boolean(filter, NULL, &scan, &cam->normalize) )
    goto cleanup ;

  switch ( cam->whitepointname ) {
  case WCS_NAME_D50:
  case WCS_NAME_D65:
  case WCS_NAME_A:
  case WCS_NAME_F2:
    break ;
  default:
    (void)error_handler(RANGECHECK) ;
    goto cleanup ;
  }

  success = TRUE ;
 cleanup :
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return success ;
}

wcsElementFuncts wcs_ColorAppearanceModel_functs[] =
{
  { XML_INTERN(ColorAppearanceModel),
    wcs_ColorAppearanceModel_Start,
    wcs_ColorAppearanceModel_End,
    NULL
  },
  { XML_INTERN(ProfileName),
    wcs_ProfileName_Start,
    NULL,
    NULL
  },
  { XML_INTERN(Description),
    wcs_Description_Start,
    NULL,
    NULL
  },
  { XML_INTERN(Author),
    wcs_Author_Start,
    NULL,
    NULL
  },
  { XML_INTERN(ViewingConditions),
    wcs_ViewingConditions_Start,
    NULL,
    NULL
  },
  { XML_INTERN(WhitePoint),
    wcs_WhitePoint_Start,
    NULL,
    NULL
  },
  { XML_INTERN(WhitePointName),
    NULL,
    wcs_WhitePointName_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Background),
    wcs_Background_Start,
    NULL,
    NULL
  },
  { XML_INTERN(ImpactOfSurround),
    NULL,
    wcs_ImpactOfSurround_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Surround),
    NULL,
    wcs_Surround_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(LuminanceOfAdaptingField),
    NULL,
    wcs_LuminanceOfAdaptingField_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(DegreeOfAdaptation),
    NULL,
    wcs_DegreeOfAdaptation_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(NormalizeToMediaWhitePoint),
    NULL,
    wcs_NormalizeToMediaWhitePoint_End,
    wcs_Buffer_Chars
  },
  WCS_ELEMENTFUNCTS_END
} ;

/*
* Log stripped */
