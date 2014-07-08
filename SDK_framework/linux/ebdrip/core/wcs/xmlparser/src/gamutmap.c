/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:gamutmap.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * GamutMapModel callbacks
 */

#include "core.h"
#include "wcsxml.h"
#include "xmlgattrs.h"
#include "xmltypeconv.h"
#include "namedef_.h"
#include "swerrors.h"

static Bool wcs_get_gmm(xmlGFilter *filter, wcsGamutMapModel **gmm)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  if ( !xml_doc )
    return error_handler(UNDEFINED) ;

  *gmm = xml_doc->model.gmm ;

  return TRUE ;
}

void wcs_destroy_gmm(sw_memory_instance *memstate, wcsGamutMapModel *gmm)
{
  if ( gmm ) {
    if ( gmm->header.profilename )
      wcscmm_free(memstate, gmm->header.profilename) ;
    if ( gmm->header.description )
      wcscmm_free(memstate, gmm->header.description) ;
    if ( gmm->header.author )
      wcscmm_free(memstate, gmm->header.author) ;

    if ( gmm->id )
      wcscmm_free(memstate, gmm->id) ;
    if ( gmm->plugingamut.guid )
      wcscmm_free(memstate, gmm->plugingamut.guid) ;

    wcscmm_free(memstate, gmm) ;
  }
}

static Bool wcs_GamutMapModel_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  static const wcsGamutMapModel init_gmm = { 0 } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ProfileName), XML_INTERN(ns_2005_02_wcs_gmm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(Description), XML_INTERN(ns_2005_02_wcs_gmm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Author), XML_INTERN(ns_2005_02_wcs_gmm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(DefaultBaselineGamutMapModel), XML_INTERN(ns_2005_02_wcs_gmm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(PlugInGamutMapModel), XML_INTERN(ns_2005_02_wcs_gmm), XMLG_G_SEQUENCED, 1},
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

  if ( xml_doc->modeltype != wcsGamutMapModelType || xml_doc->model.gmm )
    return error_handler(UNDEFINED) ;

  xml_doc->model.gmm = wcscmm_alloc(xml_doc->memstate, sizeof(wcsGamutMapModel)) ;
  if ( !xml_doc->model.gmm )
    return error_handler(VMERROR) ;

  *xml_doc->model.gmm = init_gmm ;
  xml_doc->model.gmm->id = id_set ? id : NULL ;

  return TRUE ;
}

static int32 wcs_GamutMapModel_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  wcsGamutMapModel *gmm ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !wcs_get_gmm(filter, &gmm) )
    success = FALSE ;

  if ( !success && gmm ) {
    xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
    wcs_destroy_gmm(xml_doc->memstate, gmm) ;
    xml_doc->model.gmm = NULL ;
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
  wcsGamutMapModel *gmm ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_gmm(filter, &gmm) )
    return FALSE ;

  xml_doc->textptr = &gmm->header.profilename ;

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
  wcsGamutMapModel *gmm ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_gmm(filter, &gmm) )
    return FALSE ;

  xml_doc->textptr = &gmm->header.description ;

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
  wcsGamutMapModel *gmm ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_gmm(filter, &gmm) )
    return FALSE ;

  xml_doc->textptr = &gmm->header.author ;

  return TRUE ;
}

static Bool wcs_DefaultBaselineGamutMapModel_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsGamutMapModel *gmm ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_gmm(filter, &gmm) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_name(filter, NULL, &scan, &gmm->defaultbaseline) )
    return FALSE ;

  switch ( gmm->defaultbaseline ) {
  case WCS_NAME_HPMinCD_Absolute:
  case WCS_NAME_HPMinCD_Relative:
  case WCS_NAME_SGCK:
  case WCS_NAME_HueMap:
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

static Bool wcs_PlugInGamutMapModel_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsGamutMapModel *gmm ;

  static char *guid ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(GUID), NULL, NULL, wcs_convert_string, &guid},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_gmm(filter, &gmm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  gmm->plugingamut.guid = guid ;

  /* TBD */
  HQFAIL("WCS PlugInGamutMapModel components to be determined...") ;

  return TRUE ;
}

static Bool wcs_PlugInGamutMapModel_Chars(
      xmlGFilter *filter,
      const uint8 *buf,
      uint32 buflen)
{
  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(const uint8 *, buf) ;
  UNUSED_PARAM(uint32, buflen) ;

  /* TBD */

  return TRUE ;
}

static int32 wcs_PlugInGamutMapModel_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  /* TBD */

  return success ;
}

wcsElementFuncts wcs_GamutMapModel_functs[] =
{
  { XML_INTERN(GamutMapModel),
    wcs_GamutMapModel_Start,
    wcs_GamutMapModel_End,
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
  { XML_INTERN(DefaultBaselineGamutMapModel),
    NULL,
    wcs_DefaultBaselineGamutMapModel_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(PlugInGamutMapModel),
    wcs_PlugInGamutMapModel_Start,
    wcs_PlugInGamutMapModel_End,
    wcs_PlugInGamutMapModel_Chars
  },
  WCS_ELEMENTFUNCTS_END
} ;

/*
* Log stripped */
