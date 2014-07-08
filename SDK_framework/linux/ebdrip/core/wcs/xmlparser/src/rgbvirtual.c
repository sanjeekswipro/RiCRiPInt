/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:rgbvirtual.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorDeviceModel callbacks for the RGB virtual device
 */

#include "core.h"
#include "wcsxml.h"
#include "xmlgattrs.h"
#include "xmltypeconv.h"
#include "namedef_.h"
#include "swerrors.h"

static Bool wcs_MeasurementData_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MaxColorantUsed), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(MinColorantUsed), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(WhitePrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(RedPrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(GreenPrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(BluePrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(BlackPrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(Gamma), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(GammaOffsetGain), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(GammaOffsetGainLinearGain), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(HDRToneResponseCurves), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(GamutBoundarySamples), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  static char *timestamp ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(TimeStamp), NULL, NULL, wcs_convert_string, &timestamp},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  cdm->timestamp = timestamp ;

  return TRUE ;
}

static Bool wcs_MaxColorantUsed_End(
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

  if ( !wcs_convert_float(filter, NULL, &scan, &cdm->device.rgbvirtual.maxcolorantused) )
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

static Bool wcs_MinColorantUsed_End(
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

  if ( !wcs_convert_float(filter, NULL, &scan, &cdm->device.rgbvirtual.mincolorantused) )
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

static Bool wcs_WhitePrimary_Start(
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

  cdm->device.rgbvirtual.rgbprimaries.white = cie ;

  return TRUE ;
}

static Bool wcs_RedPrimary_Start(
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

  cdm->device.rgbvirtual.rgbprimaries.red = cie ;

  return TRUE ;
}

static Bool wcs_GreenPrimary_Start(
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

  cdm->device.rgbvirtual.rgbprimaries.green = cie ;

  return TRUE ;
}

static Bool wcs_BluePrimary_Start(
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

  cdm->device.rgbvirtual.rgbprimaries.blue = cie ;

  return TRUE ;
}

static Bool wcs_BlackPrimary_Start(
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

  cdm->device.rgbvirtual.rgbprimaries.black = cie ;

  return TRUE ;
}

static Bool wcs_Gamma_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static wcsValue value ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(value), NULL, NULL, wcs_convert_nonNegFloat, &value},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  cdm->device.rgbvirtual.gamma.type = WCS_GAMMA ;
  cdm->device.rgbvirtual.gamma.gamma = value ;

  return TRUE ;
}

static Bool wcs_GammaOffsetGain_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static wcsValue gamma, offset, gain ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Gamma), NULL, NULL, wcs_convert_nonNegFloat, &gamma},
    { XML_INTERN(Offset), NULL, NULL, wcs_convert_nonNegFloat, &offset},
    { XML_INTERN(Gain), NULL, NULL, wcs_convert_nonNegFloat, &gain},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  cdm->device.rgbvirtual.gamma.type = WCS_GAMMAOFFSETGAIN ;
  cdm->device.rgbvirtual.gamma.gamma = gamma ;
  cdm->device.rgbvirtual.gamma.offset = offset ;
  cdm->device.rgbvirtual.gamma.gain = gain ;

  return TRUE ;
}

static Bool wcs_GammaOffsetGainLinearGain_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static wcsValue gamma, offset, gain, lineargain, transitionpoint ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Gamma), NULL, NULL, wcs_convert_nonNegFloat, &gamma},
    { XML_INTERN(Offset), NULL, NULL, wcs_convert_nonNegFloat, &offset},
    { XML_INTERN(Gain), NULL, NULL, wcs_convert_nonNegFloat, &gain},
    { XML_INTERN(LinearGain), NULL, NULL, wcs_convert_nonNegFloat, &lineargain},
    { XML_INTERN(TransitionPoint), NULL, NULL, wcs_convert_nonNegFloat, &transitionpoint},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  cdm->device.rgbvirtual.gamma.type = WCS_GAMMAOFFSETGAINLINEARGAIN ;
  cdm->device.rgbvirtual.gamma.gamma = gamma ;
  cdm->device.rgbvirtual.gamma.offset = offset ;
  cdm->device.rgbvirtual.gamma.gain = gain ;
  cdm->device.rgbvirtual.gamma.lineargain = lineargain ;
  cdm->device.rgbvirtual.gamma.transitionpoint = transitionpoint ;

  return TRUE ;
}

static Bool wcs_create_oneDimensionLut(xmlDocumentContext *xml_doc,
                                       wcsOneDimensionLut *lut, uint32 length)
{
  size_t bytes = length * sizeof(wcsValue) ;

  HQASSERT(lut->length == 0 && !lut->input && !lut->output, "one dimension lut already allocated") ;

  lut->length = length ;

  lut->input = wcscmm_alloc(xml_doc->memstate, bytes) ;
  if ( !lut->input )
    return error_handler(VMERROR) ;

  lut->output = wcscmm_alloc(xml_doc->memstate, bytes) ;
  if ( !lut->output )
    return error_handler(VMERROR) ;

  return TRUE ;
}

void wcs_destroy_oneDimensionLut(sw_memory_instance *memstate, wcsOneDimensionLut *lut)
{
  if ( lut->length > 0 ) {
    if ( lut->input ) {
      wcscmm_free(memstate, lut->input) ;
      lut->input = NULL ;
    }

    if ( lut->output ) {
      wcscmm_free(memstate, lut->output) ;
      lut->output = NULL ;
    }

    lut->length = 0 ;
  }
}

static Bool wcs_HDRToneResponseCurves_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(RedTRC), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(GreenTRC), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(BlueTRC), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  static int32 trclength ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(TRCLength), NULL, NULL, xml_convert_integer, &trclength},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if ( trclength < 2 || trclength > 2048 )
    return error_handler(RANGECHECK) ;

  cdm->device.rgbvirtual.gamma.type = WCS_HDRTONERESPONSECURVES ;
  if ( !wcs_create_oneDimensionLut(xml_doc, &cdm->device.rgbvirtual.gamma.hdrtrc.redtrc, trclength) ||
       !wcs_create_oneDimensionLut(xml_doc, &cdm->device.rgbvirtual.gamma.hdrtrc.greentrc, trclength) ||
       !wcs_create_oneDimensionLut(xml_doc, &cdm->device.rgbvirtual.gamma.hdrtrc.bluetrc, trclength) )
    return FALSE ;

  return TRUE ;
}

static Bool wcs_GamutBoundarySamples_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(RGB), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

static Bool wcs_RedTRC_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Input), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(Output), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  xml_doc->lutptr = &cdm->device.rgbvirtual.gamma.hdrtrc.redtrc ;

  return TRUE ;
}

static Bool wcs_GreenTRC_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Input), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(Output), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  xml_doc->lutptr = &cdm->device.rgbvirtual.gamma.hdrtrc.greentrc ;

  return TRUE ;
}

static Bool wcs_BlueTRC_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Input), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(Output), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  xml_doc->lutptr = &cdm->device.rgbvirtual.gamma.hdrtrc.bluetrc ;

  return TRUE ;
}

static Bool wcs_Input_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  uint32 remaining ;
  wcsValue *values ;
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

  (void)xml_match_whitespace(&scan) ;

  for ( remaining = xml_doc->lutptr->length, values = xml_doc->lutptr->input ;
        remaining > 0 ;
        --remaining, ++values ) {

    if ( !wcs_convert_float(filter, NULL, &scan, &values) )
      goto cleanup ;

    /** \todo It's not clear from the spec whether min/maxcolorantused
       applies to the input or output values, or both. */
    if ( *values < cdm->device.rgbvirtual.mincolorantused ||
         *values > cdm->device.rgbvirtual.maxcolorantused ) {
      (void)error_handler(RANGECHECK) ;
      goto cleanup ;
    }

    (void)xml_match_whitespace(&scan) ;
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

static Bool wcs_Output_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  uint32 remaining ;
  wcsValue *values ;
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

  (void)xml_match_whitespace(&scan) ;

  for ( remaining = xml_doc->lutptr->length, values = xml_doc->lutptr->output ;
        remaining > 0 ;
        --remaining, ++values ) {

    if ( !wcs_convert_float(filter, NULL, &scan, &values) )
      goto cleanup ;

    /** \todo It's not clear from the spec whether min/maxcolorantused
       applies to the input or output values, or both. */
    if ( *values < cdm->device.rgbvirtual.mincolorantused ||
         *values > cdm->device.rgbvirtual.maxcolorantused ) {
      (void)error_handler(RANGECHECK) ;
      goto cleanup ;
    }

    (void)xml_match_whitespace(&scan) ;
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

static Bool wcs_RGB_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  wcsRGBSample *sample ;

  static wcsRGB rgb ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(R), NULL, NULL, wcs_convert_float, &rgb.rgb[0]},
    { XML_INTERN(G), NULL, NULL, wcs_convert_float, &rgb.rgb[1]},
    { XML_INTERN(B), NULL, NULL, wcs_convert_float, &rgb.rgb[2]},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  /* Allowed an unbounded number of samples. */
  if ( cdm->device.rgbvirtual.gamutboundarysamples.samples.count == MAXUINT32 )
    return error_handler(RANGECHECK) ;

  sample = wcscmm_alloc(xml_doc->memstate, sizeof(wcsRGBSample)) ;
  if ( !sample )
    return error_handler(VMERROR) ;

  sample->value = rgb ;
  sample->next = NULL ;

  /* Prepend the sample. */
  sample->next = cdm->device.rgbvirtual.gamutboundarysamples.samples.head ;
  cdm->device.rgbvirtual.gamutboundarysamples.samples.head = sample ;
  cdm->device.rgbvirtual.gamutboundarysamples.samples.count += 1 ;

  return TRUE ;
}

wcsElementFuncts wcs_CDM_RGBVirtualDevice_functs[] =
{
  { XML_INTERN(MeasurementData),
    wcs_MeasurementData_Start,
    NULL,
    NULL
  },
  { XML_INTERN(MaxColorantUsed), 
    NULL,
    wcs_MaxColorantUsed_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(MinColorantUsed), 
    NULL,
    wcs_MinColorantUsed_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(WhitePrimary),
    wcs_WhitePrimary_Start,
    NULL,
    NULL
  },
  { XML_INTERN(RedPrimary),
    wcs_RedPrimary_Start,
    NULL,
    NULL
  },
  { XML_INTERN(GreenPrimary),
    wcs_GreenPrimary_Start,
    NULL,
    NULL
  },
  { XML_INTERN(BluePrimary),
    wcs_BluePrimary_Start,
    NULL,
    NULL
  },
  { XML_INTERN(BlackPrimary),
    wcs_BlackPrimary_Start,
    NULL,
    NULL
  },
  { XML_INTERN(Gamma),
    wcs_Gamma_Start,
    NULL,
    NULL
  },
  { XML_INTERN(GammaOffsetGain),
    wcs_GammaOffsetGain_Start,
    NULL,
    NULL
  },
  { XML_INTERN(GammaOffsetGainLinearGain),
    wcs_GammaOffsetGainLinearGain_Start,
    NULL,
    NULL
  },
  { XML_INTERN(HDRToneResponseCurves),
    wcs_HDRToneResponseCurves_Start,
    NULL,
    NULL
  },
  { XML_INTERN(GamutBoundarySamples),
    wcs_GamutBoundarySamples_Start,
    NULL,
    NULL
  },
  { XML_INTERN(RedTRC),
    wcs_RedTRC_Start,
    NULL,
    NULL
  },
  { XML_INTERN(GreenTRC),
    wcs_GreenTRC_Start,
    NULL,
    NULL
  },
  { XML_INTERN(BlueTRC),
    wcs_BlueTRC_Start,
    NULL,
    NULL
  },
  { XML_INTERN(Input),
    NULL,
    wcs_Input_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Output),
    NULL,
    wcs_Output_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Output),
    wcs_RGB_Start,
    NULL,
    NULL
  },
  WCS_ELEMENTFUNCTS_END
} ;

/*
* Log stripped */
