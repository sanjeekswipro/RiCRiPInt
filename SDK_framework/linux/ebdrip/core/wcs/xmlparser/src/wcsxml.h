/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:wcsxml.h(EBDSDK_P.1) $
 * $Id: xmlparser:src:wcsxml.h,v 1.2.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Parse a WCS XML profile stream into the appropriate WCS data structure.
 */

#ifndef __WCSXML_H__
#define __WCSXML_H__

#include "xml.h"
#include "wcs.h"
#include "wcscmmmem.h"

typedef uint32 wcsModelType ;
enum /* wcsModelType */ {
  wcsUnsetModelType,
  wcsColorDeviceModelType,
  wcsColorAppearanceModelType,
  wcsGamutMapModelType
} ;

struct xmlDocumentContext {
  sw_memory_instance        *memstate ;

  wcsModelType               modeltype ;
  union {
    wcsColorDeviceModel     *cdm ;
    wcsColorAppearanceModel *cam ;
    wcsGamutMapModel        *gmm ;
  } model ;

  /* Temporary locations used whilst building lists etc. */
  wcsOneDimensionLut        *lutptr ;
  wcsNonNegRGBSampleList    *rgbsamplesptr ;
  wcsNonNegCMYKSampleList   *cmyksamplesptr ;
  char                     **textptr ;

  /* Used to buffer repeated Chars callbacks until the End callback.
     This assumes the amount of data from the Chars callbacks isn't
     too large -  most of them are for known names or numbers. */
  utf8_buffer                charsbuffer ;
} ;

typedef struct wcsElementFuncts {
  xmlGIStr                 *localname ;
  xmlGStartElementCallback  f_start ;
  xmlGEndElementCallback    f_end ;
  xmlGCharactersCallback    f_chars ;
} wcsElementFuncts ;

/* End element for functs */
#define WCS_ELEMENTFUNCTS_END { NULL, NULL, NULL, NULL }

extern wcsElementFuncts wcs_CommonProfileTypes_functs[] ;
extern wcsElementFuncts wcs_ColorDeviceModel_functs[] ;
extern wcsElementFuncts wcs_ColorAppearanceModel_functs[] ;
extern wcsElementFuncts wcs_GamutMapModel_functs[] ;

/* These ColorDeviceModel functions are included later when device type is known. */
extern wcsElementFuncts wcs_CDM_DisplayDevice_functs[] ;
extern wcsElementFuncts wcs_CDM_RGBProjectorDevice_functs[] ;
extern wcsElementFuncts wcs_CDM_RGBCaptureDevice_functs[] ;
extern wcsElementFuncts wcs_CDM_RGBPrinterDevice_functs[] ;
extern wcsElementFuncts wcs_CDM_CMYKPrinterDevice_functs[] ;
extern wcsElementFuncts wcs_CDM_RGBVirtualDevice_functs[] ;

Bool wcs_register_cb_array(
    xmlDocumentContext *wcs_ctxt,
    xmlGFilter *filter,
    xmlGIStr *xml_namespace,
    wcsElementFuncts *funct_array) ;

Bool wcs_Buffer_Chars(
      xmlGFilter *filter,
      const uint8 *buf,
      uint32 buflen) ;

Bool wcs_get_cdm(xmlGFilter *filter, wcsColorDeviceModel **cdm) ;

Bool wcs_get_header(xmlGFilter *filter, wcsHeader **header) ;

Bool wcs_convert_boolean(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* Bool* */) ;

Bool wcs_convert_float(xmlGFilter *filter,
                       xmlGIStr *attrlocalname,
                       utf8_buffer* value,
                       void *data /* wcsValue* */) ;

Bool wcs_convert_nonNegFloat(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* wcsValue* */) ;

Bool wcs_convert_nonNegCIEXYZ(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* wcsValue* */) ;

Bool wcs_convert_string(xmlGFilter *filter,
                        xmlGIStr *attrlocalname,
                        utf8_buffer* value,
                        void *data /* char** */) ;

Bool wcs_convert_name(xmlGFilter *filter,
                      xmlGIStr *attrlocalname,
                      utf8_buffer* value,
                      void *data /* wcsNameNumber* */) ;

#endif

/*
* Log stripped */
