/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:display.c(EBDSDK_P.1) $
 * $Id: xmlparser:src:display.c,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorDeviceModel callbacks for the CRT and LCD devices
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
    { XML_INTERN(WhitePrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(RedPrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(GreenPrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(BluePrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(BlackPrimary), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(GrayRamp), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(RedRamp), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(GreenRamp), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(BlueRamp), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
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

  cdm->device.display.rgbprimaries.white = cie ;

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

  cdm->device.display.rgbprimaries.red = cie ;

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

  cdm->device.display.rgbprimaries.green = cie ;

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

  cdm->device.display.rgbprimaries.blue = cie ;

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

  cdm->device.display.rgbprimaries.black = cie ;

  return TRUE ;
}

static Bool wcs_GrayRamp_Start(
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

  xml_doc->rgbsamplesptr = &cdm->device.display.grayramp ;

  return TRUE ;
}

static Bool wcs_RedRamp_Start(
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

  xml_doc->rgbsamplesptr = &cdm->device.display.redramp ;

  return TRUE ;
}

static Bool wcs_GreenRamp_Start(
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

  xml_doc->rgbsamplesptr = &cdm->device.display.greenramp ;

  return TRUE ;
}

static Bool wcs_BlueRamp_Start(
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

  xml_doc->rgbsamplesptr = &cdm->device.display.blueramp ;

  return TRUE ;
}

static Bool wcs_Sample_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  return wcs_RGBSampleWithMaxOccurs_Start(filter, localname, prefix, uri, attrs, 4096) ;
}

wcsElementFuncts wcs_CDM_DisplayDevice_functs[] =
{
  { XML_INTERN(MeasurementData),
    wcs_MeasurementData_Start,
    NULL,
    NULL
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
  { XML_INTERN(GrayRamp),
    wcs_GrayRamp_Start,
    NULL,
    NULL
  },
  { XML_INTERN(RedRamp),
    wcs_RedRamp_Start,
    NULL,
    NULL
  },
  { XML_INTERN(GreenRamp),
    wcs_GreenRamp_Start,
    NULL,
    NULL
  },
  { XML_INTERN(BlueRamp),
    wcs_BlueRamp_Start,
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
