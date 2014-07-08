/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:commontypes.c(EBDSDK_P.1) $
 * $Id: xmlparser:src:commontypes.c,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * WcsCommonProfileTypes callbacks and converters
 */

#include "core.h"
#include "wcsxml.h"
#include "xmlgattrs.h"
#include "xmltypeconv.h"
#include "namedef_.h"
#include "swerrors.h"
#include "hqmemcpy.h"

/* Strangely, WCS does not use the standard XSD bool. */
Bool wcs_convert_boolean(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* Bool* */)
{
  utf8_buffer scan ;
  Bool *flag = data ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT((value != NULL),
           "wcs_convert_boolean: NULL utf8 buffer pointer") ;
  HQASSERT(flag != NULL, "Nowhere to put converted boolean") ;

  scan = *value;

  if ( scan.unitlength == 5 &&
       scan.codeunits[0] == 'F' &&
       scan.codeunits[1] == 'a' &&
       scan.codeunits[2] == 'l' &&
       scan.codeunits[3] == 's' &&
       scan.codeunits[4] == 'e' ) {
    *flag = FALSE ;
    scan.codeunits += 5 ;
    scan.unitlength -= 5 ;
  } else if ( scan.unitlength == 4 &&
              scan.codeunits[0] == 'T' &&
              scan.codeunits[1] == 'r' &&
              scan.codeunits[2] == 'u' &&
              scan.codeunits[3] == 'e' ) {
    *flag = TRUE ;
    scan.codeunits += 4 ;
    scan.unitlength -= 4 ;
  } else
    return error_handler(SYNTAXERROR) ;

  *value = scan ;
  return TRUE ;
}

Bool wcs_convert_float(xmlGFilter *filter,
                       xmlGIStr *attrlocalname,
                       utf8_buffer* value,
                       void *data /* wcsValue* */)
{
  float number ;

  if ( !xml_convert_float(filter, attrlocalname, value, &number) )
    return FALSE ;

  *(wcsValue*)data = number ;

  return TRUE ;
}

Bool wcs_convert_nonNegFloat(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* wcsValue* */)
{
  float number ;

  if ( !xml_convert_float(filter, attrlocalname, value, &number) )
    return FALSE ;

  if ( number < 0 )
    return error_handler(RANGECHECK) ;

  *(wcsValue*)data = number ;

  return TRUE ;
}

Bool wcs_convert_nonNegCIEXYZ(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* wcsValue* */)
{
  float number ;

  if ( !xml_convert_float(filter, attrlocalname, value, &number) )
    return FALSE ;

  if ( number < 0 || number > 10000 )
    return error_handler(RANGECHECK) ;

  *(wcsValue*)data = number ;

  return TRUE ;
}

Bool wcs_convert_string(xmlGFilter *filter,
                        xmlGIStr *attrlocalname,
                        utf8_buffer* value,
                        void *data /* char** */)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  char **str = data ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(str, "Nowhere to put string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");

  if ( value->unitlength == 0 ) {
    *str = NULL ;
    return TRUE ;
  }

  *str = wcscmm_alloc(xml_doc->memstate, sizeof(char) * (value->unitlength + 1)) ;
  if ( !*str )
    return error_handler(VMERROR) ;

  HqMemCpy(*str, value->codeunits, value->unitlength) ;
  (*str)[value->unitlength] = '\0' ;

  value->codeunits += value->unitlength ;
  value->unitlength = 0 ;

  return TRUE ;
}

Bool wcs_convert_name(xmlGFilter *filter,
                      xmlGIStr *attrlocalname,
                      utf8_buffer* value,
                      void *data /* wcsNameNumber* */)
{
  wcsNameNumber *name = data ;
  xmlGIStr *intern ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(name, "Nowhere to put name") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");

  /* Lookup, but don't bother caching if it's not there already. */
  if ( !intern_lookup(&intern, value->codeunits, value->unitlength) )
    return FALSE ;

  /* Map Core NAMECACHE numbers to libwcs wcsNameNumbers. */
  switch ( XML_INTERN_SWITCH(intern) ) {
  case NAME_CIEXYZ :
    *name = WCS_NAME_CIEXYZ ; break ;
  case NAME_D50 :
    *name = WCS_NAME_D50 ; break ;
  case NAME_D65 :
    *name = WCS_NAME_D65 ; break ;
  case NAME_A :
    *name = WCS_NAME_A ; break ;
  case NAME_F2 :
    *name = WCS_NAME_F2 ; break ;
  case NAME_0_45 :
    *name = WCS_NAME_0_45 ; break ;
  case NAME_0_diffuse :
    *name = WCS_NAME_0_diffuse ; break ;
  case NAME_diffuse_0 :
    *name = WCS_NAME_diffuse_0 ; break ;
  case NAME_direct :
    *name = WCS_NAME_direct ; break ;
  case NAME_Average :
    *name = WCS_NAME_Average ; break ;
  case NAME_Dim :
    *name = WCS_NAME_Dim ; break ;
  case NAME_Dark :
    *name = WCS_NAME_Dark ; break ;
  case NAME_HPMinCD_Absolute :
    *name = WCS_NAME_HPMinCD_Absolute ; break ;
  case NAME_HPMinCD_Relative :
    *name = WCS_NAME_HPMinCD_Relative ; break ;
  case NAME_SGCK :
    *name = WCS_NAME_SGCK ; break ;
  case NAME_HueMap :
    *name = WCS_NAME_HueMap ; break ;
  default : return error_handler(RANGECHECK) ;
  }

  value->codeunits += value->unitlength ;
  value->unitlength = 0 ;

  return TRUE ;
}

static Bool wcs_Text_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  utf8_buffer scan ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  if ( !success )
    goto cleanup ;

  success = FALSE ; /* Until everything below is ok */

  scan.codeunits = xml_doc->charsbuffer.codeunits ;
  scan.unitlength = xml_doc->charsbuffer.unitlength ;

  if ( !wcs_convert_string(filter, NULL, &scan, xml_doc->textptr) )
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

Bool wcs_Buffer_Chars(
      xmlGFilter *filter,
      const uint8 *buf,
      uint32 buflen)
{
  xmlDocumentContext *xml_doc = xmlg_get_user_data(filter) ;
  utf8_buffer charsbuffer ;

  charsbuffer.unitlength = xml_doc->charsbuffer.unitlength + buflen ;
  charsbuffer.codeunits = wcscmm_alloc(xml_doc->memstate, charsbuffer.unitlength * sizeof(UTF8)) ;
  if ( !charsbuffer.codeunits )
    return error_handler(VMERROR) ;

  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    HqMemCpy(charsbuffer.codeunits, xml_doc->charsbuffer.codeunits, xml_doc->charsbuffer.unitlength * sizeof(UTF8)) ;
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
  }

  HqMemCpy(charsbuffer.codeunits + xml_doc->charsbuffer.unitlength, buf, buflen) ;

  xml_doc->charsbuffer = charsbuffer ;

  return TRUE ;
}

wcsElementFuncts wcs_CommonProfileTypes_functs[] =
{
  { XML_INTERN(Text),
    NULL,
    wcs_Text_End,
    wcs_Buffer_Chars
  },
  WCS_ELEMENTFUNCTS_END
} ;

/*
* Log stripped */
