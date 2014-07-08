/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:rgbsamples.c(EBDSDK_P.1) $
 * $Id: xmlparser:src:rgbsamples.c,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorDeviceModel callbacks for wcsNonNegRGBSamples
 */

#include "core.h"
#include "wcsxml.h"
#include "xmlgattrs.h"
#include "xmltypeconv.h"
#include "namedef_.h"
#include "swerrors.h"

Bool wcs_RGBSampleWithMaxOccurs_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      uint32 maxoccurs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsNonNegRGBSample sample_default = {0} ;
  wcsNonNegRGBSample *sample ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(RGB), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
    { XML_INTERN(CIEXYZ), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_G_SEQUENCED_ONE, 1},
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

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  if ( xml_doc->rgbsamplesptr->count == maxoccurs )
    return error_handler(RANGECHECK) ;

  sample = wcscmm_alloc(xml_doc->memstate, sizeof(wcsNonNegRGBSample)) ;
  if ( !sample )
    return error_handler(VMERROR) ;

  *sample = sample_default ;
  sample->tag = tag_set ? tag : NULL ;

  /* Prepend the sample. */
  sample->next = xml_doc->rgbsamplesptr->head ;
  xml_doc->rgbsamplesptr->head = sample ;
  xml_doc->rgbsamplesptr->count += 1 ;

  return TRUE ;
}

Bool wcs_RGB_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  static wcsNonNegRGB rgb ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(R), NULL, NULL, wcs_convert_nonNegFloat, &rgb.rgb[0]},
    { XML_INTERN(G), NULL, NULL, wcs_convert_nonNegFloat, &rgb.rgb[1]},
    { XML_INTERN(B), NULL, NULL, wcs_convert_nonNegFloat, &rgb.rgb[2]},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  /* Should have allocated a wcsNonNegRGBSample in wcs_Sample_Start. */
  if ( !xml_doc->rgbsamplesptr->head )
    return error_handler(UNREGISTERED) ;

  xml_doc->rgbsamplesptr->head->rgb = rgb ;

  return TRUE ;
}

Bool wcs_RGBCIEXYZ_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

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

  /* Should have allocated a wcsNonNegRGBSample in wcs_Sample_Start. */
  if ( !xml_doc->rgbsamplesptr->head )
    return error_handler(UNREGISTERED) ;

  xml_doc->rgbsamplesptr->head->ciexyz = cie ;

  return TRUE ;
}

void wcs_destroy_nonnegrgbsamples(sw_memory_instance *memstate, wcsNonNegRGBSampleList *samples)
{
  while ( samples->head ) {
    wcsNonNegRGBSample *sample = samples->head ;
    samples->head = sample->next ;
    if ( sample->tag )
      wcscmm_free(memstate, sample->tag) ;
    wcscmm_free(memstate, sample) ;
  }
}

void wcs_destroy_rgbsamples(sw_memory_instance *memstate, wcsRGBList *samples)
{
  while ( samples->head ) {
    wcsRGBSample *sample = samples->head ;
    samples->head = sample->next ;
    wcscmm_free(memstate, sample) ;
  }
}

/*
* Log stripped */
