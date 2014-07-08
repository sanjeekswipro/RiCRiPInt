/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:rgbprinter.c(EBDSDK_P.1) $
 * $Id: xmlparser:src:rgbprinter.c,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorDeviceModel callbacks for the RGB printer device
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
    { XML_INTERN(ColorCube), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
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

static Bool wcs_ColorCube_Start(
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

  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !xmlg_valid_children(filter, localname, uri, valid_children) )
    return error_handler(UNDEFINED) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  xml_doc->rgbsamplesptr = &cdm->device.rgbprinter.colorcube.samples ;

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

wcsElementFuncts wcs_CDM_RGBPrinterDevice_functs[] =
{
  { XML_INTERN(MeasurementData),
    wcs_MeasurementData_Start,
    NULL,
    NULL
  },
  { XML_INTERN(ColorCube),
    wcs_ColorCube_Start,
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
