/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:rgbcapture.c(EBDSDK_P.1) $
 * $Id: xmlparser:src:rgbcapture.c,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorDeviceModel callbacks for the scanner and camera devices
 */

#include "core.h"
#include "wcsxml.h"
#include "xmlgattrs.h"
#include "xmltypeconv.h"
#include "namedef_.h"
#include "swerrors.h"
#include "rgbsamples.h"

static Bool wcs_MeasurementData_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(PrimaryIndex), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(NeutralIndices), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(ColorSamples), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
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

static Bool wcs_PrimaryIndex_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static XMLG_VALID_CHILDREN valid_children[] = {
    /* Specified with the 'All' indicator which means any order allowed. */
    { XML_INTERN(White), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP},
    { XML_INTERN(Black), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    { XML_INTERN(Red), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    { XML_INTERN(Green), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    { XML_INTERN(Blue), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    { XML_INTERN(Cyan), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    { XML_INTERN(Magenta), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    { XML_INTERN(Yellow), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

static Bool wcs_White_End(
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

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->device.rgbcapture.primaryindex.white) )
    goto cleanup ;

  if ( cdm->device.rgbcapture.primaryindex.white < 1 ) {
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

static Bool wcs_Black_End(
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

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->device.rgbcapture.primaryindex.black) )
    goto cleanup ;

  if ( cdm->device.rgbcapture.primaryindex.black < 1 ) {
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

static Bool wcs_Red_End(
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

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->device.rgbcapture.primaryindex.red) )
    goto cleanup ;

  if ( cdm->device.rgbcapture.primaryindex.red < 1 ) {
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

static Bool wcs_Green_End(
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

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->device.rgbcapture.primaryindex.green) )
    goto cleanup ;

  if ( cdm->device.rgbcapture.primaryindex.green < 1 ) {
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

static Bool wcs_Blue_End(
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

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->device.rgbcapture.primaryindex.blue) )
    goto cleanup ;

  if ( cdm->device.rgbcapture.primaryindex.blue < 1 ) {
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

static Bool wcs_Cyan_End(
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

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->device.rgbcapture.primaryindex.cyan) )
    goto cleanup ;

  if ( cdm->device.rgbcapture.primaryindex.cyan < 1 ) {
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

static Bool wcs_Magenta_End(
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

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->device.rgbcapture.primaryindex.magenta) )
    goto cleanup ;

  if ( cdm->device.rgbcapture.primaryindex.magenta < 1 ) {
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

static Bool wcs_Yellow_End(
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

  if ( !xml_convert_integer(filter, NULL, &scan, &cdm->device.rgbcapture.primaryindex.yellow) )
    goto cleanup ;

  if ( cdm->device.rgbcapture.primaryindex.yellow < 1 ) {
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

static int32 wcs_NeutralIndices_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsColorDeviceModel *cdm ;
  utf8_buffer scan ;
  uint32 count ;
  int32 value, *values = NULL ;

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

  for ( count = 0 ; scan.unitlength > 0 ; ++count ) {

    if ( !xml_convert_integer(filter, NULL, &scan, &value) )
      goto cleanup ;

    if ( value < 1 ) {
      (void)error_handler(RANGECHECK) ;
      goto cleanup ;
    }

    (void)xml_match_whitespace(&scan) ;
  }

  if ( count > 0 ) {
    values = wcscmm_alloc(xml_doc->memstate, count * sizeof(int32)) ;
    if ( !values ) {
      (void)error_handler(VMERROR) ;
      goto cleanup ;
    }
  }

  cdm->device.rgbcapture.neutralindices = values ;
  cdm->device.rgbcapture.neutralindiceslength = count ;

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  (void)xml_match_whitespace(&scan) ;

  for ( ; count > 0 ; --count, ++values ) {

    if ( !xml_convert_integer(filter, NULL, &scan, values) )
      goto cleanup ;

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

static Bool wcs_ColorSamples_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Sample), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static Bool tag_set ;
  static char *tag ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Tag), NULL, &tag_set, wcs_convert_string, &tag},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  xml_doc->rgbsamplesptr = &cdm->device.rgbcapture.colorsamples ;

  return TRUE ;
}

static Bool wcs_Sample_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  return wcs_RGBSampleWithMaxOccurs_Start(filter, localname, prefix, uri, attrs, MAXUINT32) ;
}

wcsElementFuncts wcs_CDM_RGBCaptureDevice_functs[] =
{
  { XML_INTERN(MeasurementData),
    wcs_MeasurementData_Start,
    NULL,
    NULL
  },
  { XML_INTERN(PrimaryIndex),
    wcs_PrimaryIndex_Start,
    NULL,
    NULL
  },
  { XML_INTERN(White),
    NULL,
    wcs_White_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Black),
    NULL,
    wcs_Black_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Red),
    NULL,
    wcs_Red_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Blue),
    NULL,
    wcs_Blue_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Green),
    NULL,
    wcs_Green_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Blue),
    NULL,
    wcs_Blue_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Cyan),
    NULL,
    wcs_Cyan_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Magenta),
    NULL,
    wcs_Magenta_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(Yellow),
    NULL,
    wcs_Yellow_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(NeutralIndices),
    NULL,
    wcs_NeutralIndices_End,
    wcs_Buffer_Chars
  },
  { XML_INTERN(ColorSamples),
    wcs_ColorSamples_Start,
    NULL,
    NULL
  },
  { XML_INTERN(Sample),
    wcs_Sample_Start,
    NULL,
    NULL
  },
  { XML_INTERN(RGB),
    wcs_RGB_Start,
    NULL,
    NULL
  },
  { XML_INTERN(CIEXYZ),
    wcs_RGBCIEXYZ_Start,
    NULL,
    NULL
  },
  WCS_ELEMENTFUNCTS_END
} ;

/*
* Log stripped */
