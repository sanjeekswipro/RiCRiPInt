/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:cmykprinter.c(EBDSDK_P.1) $
 * $Id: xmlparser:src:cmykprinter.c,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorDeviceModel callbacks for the CMYK printer device
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
  wcsCMYKColorCube colorcube_init = {0} ;
  wcsCMYKColorCube *colorcube ;

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

  colorcube = wcscmm_alloc(xml_doc->memstate, sizeof(wcsCMYKColorCube)) ;
  if ( !colorcube )
    return error_handler(VMERROR) ;
  *colorcube = colorcube_init ;

  /* Prepend the colorcube. */
  colorcube->next = cdm->device.cmykprinter.colorcubes.head ;
  cdm->device.cmykprinter.colorcubes.head = colorcube ;
  cdm->device.cmykprinter.colorcubes.count += 1 ;

  xml_doc->cmyksamplesptr = &cdm->device.cmykprinter.colorcubes.head->samples ;

  return TRUE ;
}

static Bool wcs_Sample_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  wcsNonNegCMYKSample sample_init = {0} ;
  wcsNonNegCMYKSample *sample ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(CMYK), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(CIEXYZ), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE_OR_MORE, XMLG_NO_GROUP},
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

  if ( xml_doc->cmyksamplesptr->count == MAXUINT32 )
    return error_handler(RANGECHECK) ;

  sample = wcscmm_alloc(xml_doc->memstate, sizeof(wcsNonNegCMYKSample)) ;
  if ( !sample )
    return error_handler(VMERROR) ;
  *sample = sample_init ;
  sample->tag = tag_set ? tag : NULL ;

  /* Prepend the sample. */
  sample->next = xml_doc->cmyksamplesptr->head ;
  xml_doc->cmyksamplesptr->head = sample ;
  xml_doc->cmyksamplesptr->count += 1 ;

  return TRUE ;
}

Bool wcs_CMYK_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  wcsColorDeviceModel *cdm ;
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;

  static wcsNonNegCMYK cmyk ;
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(C), NULL, NULL, wcs_convert_nonNegFloat, &cmyk.cmyk[0]},
    { XML_INTERN(M), NULL, NULL, wcs_convert_nonNegFloat, &cmyk.cmyk[1]},
    { XML_INTERN(Y), NULL, NULL, wcs_convert_nonNegFloat, &cmyk.cmyk[2]},
    { XML_INTERN(K), NULL, NULL, wcs_convert_nonNegFloat, &cmyk.cmyk[3]},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !wcs_get_cdm(filter, &cdm) )
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  /* Should have allocated a wcsNonNegCMYKSample in wcs_Sample_Start. */
  if ( !xml_doc->cmyksamplesptr->head )
    return error_handler(UNREGISTERED) ;

  xml_doc->cmyksamplesptr->head->cmyk = cmyk ;

  return TRUE ;
}

Bool wcs_CIEXYZ_Start(
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

  /* Should have allocated a wcsNonNegCMYKSample in wcs_Sample_Start. */
  if ( !xml_doc->cmyksamplesptr->head )
    return error_handler(UNREGISTERED) ;

  xml_doc->cmyksamplesptr->head->ciexyz = cie ;

  return TRUE ;
}

void wcs_destroy_colorcubes(sw_memory_instance *memstate, wcsCMYKColorCubeList *colorcubes)
{
  while ( colorcubes->head ) {
    wcsCMYKColorCube *colorcube = colorcubes->head ;
    colorcubes->head = colorcube->next ;

    while ( colorcube->samples.head ) {
      wcsNonNegCMYKSample *sample = colorcube->samples.head ;
      colorcube->samples.head = sample->next ;
      if ( sample->tag )
        wcscmm_free(memstate, sample->tag) ;
      wcscmm_free(memstate, sample) ;
    }

    wcscmm_free(memstate, colorcube) ;
  }
}

wcsElementFuncts wcs_CDM_CMYKPrinterDevice_functs[] =
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
  { XML_INTERN(CMYK),
    wcs_CMYK_Start,
    NULL,
    NULL
  },
  { XML_INTERN(CIEXYZ),
    wcs_CIEXYZ_Start,
    NULL,
    NULL
  },
  WCS_ELEMENTFUNCTS_END
} ;

/*
* Log stripped */
