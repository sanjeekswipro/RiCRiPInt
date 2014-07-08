/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:colordevice.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorDeviceModel callbacks
 */

#include "core.h"
#include "wcsxml.h"
#include "wcsxmlparser.h"
#include "xmlgattrs.h"
#include "xmltypeconv.h"
#include "namedef_.h"
#include "swerrors.h"

Bool wcs_get_cdm(xmlGFilter *filter, wcsColorDeviceModel **cdm)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  if ( !xml_doc )
    return error_handler(UNDEFINED) ;

  *cdm = xml_doc->model.cdm ;

  return TRUE ;
}

void wcs_destroy_oneDimensionLut(sw_memory_instance *memstate, wcsOneDimensionLut *lut) ;
void wcs_destroy_colorcubes(sw_memory_instance *memstate, wcsCMYKColorCubeList *colorcubes) ;
void wcs_destroy_nonnegrgbsamples(sw_memory_instance *memstate, wcsNonNegRGBSampleList *samples) ;
void wcs_destroy_rgbsamples(sw_memory_instance *memstate, wcsRGBList *samples) ;

void wcs_destroy_cdm(sw_memory_instance *memstate, wcsColorDeviceModel *cdm)
{
  if ( cdm ) {
    if ( cdm->header.profilename )
      wcscmm_free(memstate, cdm->header.profilename) ;
    if ( cdm->header.description )
      wcscmm_free(memstate, cdm->header.description) ;
    if ( cdm->header.author )
      wcscmm_free(memstate, cdm->header.author) ;

    switch ( cdm->devicetype ) {
    case wcsCRTDeviceType :
    case wcsLCDDeviceType :
      wcs_destroy_nonnegrgbsamples(memstate, &cdm->device.display.grayramp) ;
      wcs_destroy_nonnegrgbsamples(memstate, &cdm->device.display.redramp) ;
      wcs_destroy_nonnegrgbsamples(memstate, &cdm->device.display.greenramp) ;
      wcs_destroy_nonnegrgbsamples(memstate, &cdm->device.display.blueramp) ;
      break ;
    case wcsRGBProjectorDeviceType :
      wcs_destroy_nonnegrgbsamples(memstate, &cdm->device.rgbprojector.colorsamples) ;
      break ;
    case wcsScannerDeviceType :
    case wcsCameraDeviceType :
      wcscmm_free(memstate, cdm->device.rgbcapture.neutralindices) ;
      wcs_destroy_nonnegrgbsamples(memstate, &cdm->device.rgbcapture.colorsamples) ;
      break ;
    case wcsRGBPrinterDeviceType :
      wcs_destroy_nonnegrgbsamples(memstate, &cdm->device.rgbprinter.colorcube.samples) ;
      break ;
    case wcsCMYKPrinterDeviceType :
      wcs_destroy_colorcubes(memstate, &cdm->device.cmykprinter.colorcubes) ;
      break ;
    case wcsRGBVirtualDeviceType :
      wcs_destroy_oneDimensionLut(memstate, &cdm->device.rgbvirtual.gamma.hdrtrc.redtrc) ;
      wcs_destroy_oneDimensionLut(memstate, &cdm->device.rgbvirtual.gamma.hdrtrc.greentrc) ;
      wcs_destroy_oneDimensionLut(memstate, &cdm->device.rgbvirtual.gamma.hdrtrc.bluetrc) ;
      wcs_destroy_rgbsamples(memstate, &cdm->device.rgbvirtual.gamutboundarysamples.samples) ;
      break ;
    default :
      HQFAIL("Unrecognised WCS color device type") ;
    }

    if ( cdm->plugindevice.guid )
      wcscmm_free(memstate, cdm->plugindevice.guid) ;
    if ( cdm->timestamp )
      wcscmm_free(memstate, cdm->timestamp) ;
    if ( cdm->id )
      wcscmm_free(memstate, cdm->id) ;

    wcscmm_free(memstate, cdm) ;
  }
}

static Bool wcs_ColorDeviceModel_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  static const wcsColorDeviceModel init_cdm = { 0 } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ProfileName), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(Description), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Author), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(MeasurementConditions), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(SelfLuminous), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(MaxColorant), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(MinColorant), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(CRTDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(LCDDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RGBProjectorDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(ScannerDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(CameraDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RGBPrinterDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(CMYKPrinterDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RGBVirtualDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(PlugInDevice), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
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

  if ( xml_doc->modeltype != wcsColorDeviceModelType || xml_doc->model.cdm )
    return error_handler(UNDEFINED) ;

  xml_doc->model.cdm = wcscmm_alloc(xml_doc->memstate, sizeof(wcsColorDeviceModel)) ;
  if ( !xml_doc->model.cdm )
    return error_handler(VMERROR) ;

  *xml_doc->model.cdm = init_cdm ;
  xml_doc->model.cdm->id = id_set ? id : NULL ;

  return TRUE ;
}

static int32 wcs_ColorDeviceModel_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  wcsColorDeviceModel *cdm ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    success = FALSE ;

  if ( !success && cdm ) {
    xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
    wcs_destroy_cdm(xml_doc->memstate, cdm) ;
    xml_doc->model.cdm = NULL ;
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
  wcsColorDeviceModel *cdm ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  xml_doc->textptr = &cdm->header.profilename ;

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
  wcsColorDeviceModel *cdm ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  xml_doc->textptr = &cdm->header.description ;

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
  wcsColorDeviceModel *cdm ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  xml_doc->textptr = &cdm->header.author ;

  return TRUE ;
}

static Bool wcs_MeasurementConditions_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ColorSpace), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(WhitePoint), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ZERO_OR_ONE_OF, 1},
    { XML_INTERN(WhitePointName), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ZERO_OR_ONE_OF, 1},
    { XML_INTERN(Geometry), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(ApertureSize), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

static Bool wcs_ColorSpace_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cdm(filter, &cdm) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_name(filter, NULL, &scan, &cdm->measurementconditions.colorspace) )
    goto cleanup ;

  switch ( cdm->measurementconditions.colorspace ) {
  case WCS_NAME_CIEXYZ:
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

static Bool wcs_WhitePoint_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static wcsNonNegCIEXYZ cie ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(X), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[0]},
    { XML_INTERN(Y), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[1]},
    { XML_INTERN(Z), NULL, NULL, wcs_convert_nonNegCIEXYZ, &cie.XYZ[2]},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  cdm->measurementconditions.whitepoint = cie ;

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
  wcsColorDeviceModel *cdm ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cdm(filter, &cdm) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_name(filter, NULL, &scan, &cdm->measurementconditions.whitepointname) )
    goto cleanup ;

  switch ( cdm->measurementconditions.whitepointname ) {
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

static Bool wcs_Geometry_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cdm(filter, &cdm) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_name(filter, NULL, &scan, &cdm->measurementconditions.geometry) )
    goto cleanup ;

  switch ( cdm->measurementconditions.geometry ) {
  case WCS_NAME_0_45:
  case WCS_NAME_0_diffuse:
  case WCS_NAME_diffuse_0:
  case WCS_NAME_direct:
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

static Bool wcs_ApertureSize_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cdm(filter, &cdm) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->measurementconditions.aperturesize) )
    goto cleanup ;


  success = TRUE ;
 cleanup :
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return success ;
}

static Bool wcs_SelfLuminous_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cdm(filter, &cdm) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !xml_convert_boolean(filter, NULL, &scan, &cdm->selfluminous) )
    goto cleanup ;

  success = TRUE ;
 cleanup :
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return success ;
}

static Bool wcs_MaxColorant_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cdm(filter, &cdm) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_float(filter, NULL, &scan, &cdm->maxcolorant) )
    goto cleanup ;

  if ( cdm->maxcolorant < 0 ) {
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

static Bool wcs_MinColorant_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  if ( !wcs_get_cdm(filter, &cdm) )
    goto cleanup ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_float(filter, NULL, &scan, &cdm->mincolorant) )
    goto cleanup ;

  if ( cdm->maxcolorant <= cdm->mincolorant ) {
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

static Bool wcs_CRTDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MeasurementData), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  cdm->devicetype = wcsCRTDeviceType ;

  if ( !wcs_register_cb_array(xmlg_get_user_data(filter), filter,
                              XML_INTERN(ns_2005_02_wcs_cdm),
                              wcs_CDM_DisplayDevice_functs) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_LCDDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MeasurementData), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  cdm->devicetype = wcsLCDDeviceType ;

  if ( !wcs_register_cb_array(xmlg_get_user_data(filter), filter,
                              XML_INTERN(ns_2005_02_wcs_cdm),
                              wcs_CDM_DisplayDevice_functs) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_RGBProjectorDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MeasurementData), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  cdm->devicetype = wcsRGBProjectorDeviceType ;

  if ( !wcs_register_cb_array(xmlg_get_user_data(filter), filter,
                              XML_INTERN(ns_2005_02_wcs_cdm),
                              wcs_CDM_RGBProjectorDevice_functs) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_ScannerDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MeasurementData), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  cdm->devicetype = wcsScannerDeviceType ;

  if ( !wcs_register_cb_array(xmlg_get_user_data(filter), filter,
                              XML_INTERN(ns_2005_02_wcs_cdm),
                              wcs_CDM_RGBCaptureDevice_functs) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_CameraDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MeasurementData), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  cdm->devicetype = wcsCameraDeviceType ;

  if ( !wcs_register_cb_array(xmlg_get_user_data(filter), filter,
                              XML_INTERN(ns_2005_02_wcs_cdm),
                              wcs_CDM_RGBCaptureDevice_functs) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_RGBPrinterDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MeasurementData), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  cdm->devicetype = wcsRGBProjectorDeviceType ;

  if ( !wcs_register_cb_array(xmlg_get_user_data(filter), filter,
                              XML_INTERN(ns_2005_02_wcs_cdm),
                              wcs_CDM_RGBPrinterDevice_functs) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_CMYKPrinterDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MeasurementData), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  cdm->devicetype = wcsCMYKPrinterDeviceType ;

  if ( !wcs_register_cb_array(xmlg_get_user_data(filter), filter,
                              XML_INTERN(ns_2005_02_wcs_cdm),
                              wcs_CDM_CMYKPrinterDevice_functs) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_RGBVirtualDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MeasurementData), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  cdm->devicetype = wcsRGBVirtualDeviceType ;

  if ( !wcs_register_cb_array(xmlg_get_user_data(filter), filter,
                              XML_INTERN(ns_2005_02_wcs_cdm),
                              wcs_CDM_RGBVirtualDevice_functs) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_PlugInDevice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static char *guid ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(GUID), NULL, NULL, wcs_convert_string, &guid},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  cdm->plugindevice.guid = guid ;

  /* TBD */
  HQFAIL("WCS PlugInDevice components to be determined...") ;

  return TRUE ;
}

static Bool wcs_PlugInDevice_Chars(
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

static int32 wcs_PlugInDevice_End(
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

wcsElementFuncts wcs_ColorDeviceModel_functs[] =
{
  { XML_INTERN(ColorDeviceModel),
    wcs_ColorDeviceModel_Start,
    wcs_ColorDeviceModel_End,
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
  { XML_INTERN(MeasurementConditions),
    wcs_MeasurementConditions_Start,
    NULL,
    NULL
  },
  { XML_INTERN(ColorSpace),
    NULL,
    wcs_ColorSpace_End,
    wcs_Buffer_Chars
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
  { XML_INTERN(Geometry),
    NULL,
    wcs_Geometry_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(ApertureSize),
    NULL,
    wcs_ApertureSize_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(SelfLuminous),
    NULL,
    wcs_SelfLuminous_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(MaxColorant),
    NULL,
    wcs_MaxColorant_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(MinColorant),
    NULL,
    wcs_MinColorant_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(CRTDevice),
    wcs_CRTDevice_Start,
    NULL,
    NULL
  },
  { XML_INTERN(LCDDevice),
    wcs_LCDDevice_Start,
    NULL,
    NULL
  },
  { XML_INTERN(RGBProjectorDevice),
    wcs_RGBProjectorDevice_Start,
    NULL,
    NULL
  },
  { XML_INTERN(ScannerDevice),
    wcs_ScannerDevice_Start,
    NULL,
    NULL
  },
  { XML_INTERN(CameraDevice),
    wcs_CameraDevice_Start,
    NULL,
    NULL
  },
  { XML_INTERN(RGBPrinterDevice),
    wcs_RGBPrinterDevice_Start,
    NULL,
    NULL
  },
  { XML_INTERN(CMYKPrinterDevice),
    wcs_CMYKPrinterDevice_Start,
    NULL,
    NULL
  },
  { XML_INTERN(RGBVirtualDevice),
    wcs_RGBVirtualDevice_Start,
    NULL,
    NULL
  },
  { XML_INTERN(PlugInDevice),
    wcs_PlugInDevice_Start,
    wcs_PlugInDevice_End,
    wcs_PlugInDevice_Chars
  },
  WCS_ELEMENTFUNCTS_END
} ;

/*
* Log stripped */
