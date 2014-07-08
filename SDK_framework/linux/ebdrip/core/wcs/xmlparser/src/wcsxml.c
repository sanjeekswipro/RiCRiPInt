/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:wcsxml.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Parse a WCS XML profile stream into the appropriate WCS data structure.
 */

#include "core.h"
#include "xml.h"
#include "wcsxml.h"
#include "wcsxmlparser.h"
#include "swerrors.h"
#include "fileio.h"
#include "namedef_.h"
#include "dicthash.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "swctype.h"  /* tolower(), toupper() */

Bool wcscmm_error_occured = FALSE ;

/** \todo The error message for WCS is not very helpful currently,
   as it is just derived from the uri for filter delimiting each profile. */
#define SHORTFORM ("WCS: %.*s; Line: %d; Column: %d.")
#define LONGFORM ("WCS: %.*s; Line: %d; Column: %d; XMLInfo: %.*s")

static Bool get_error_detail(
      uint8 **detail,
      int32 *detail_len)
{
  OBJECT *errdict ;
  OBJECT *errparams ;
  OBJECT *errinfo ;

  HQASSERT(detail != NULL, "detail is NULL") ;
  HQASSERT(detail_len != NULL, "detail_len is NULL") ;

  *detail = NULL ;
  *detail_len = 0 ;

  /* get $error from systemdict */
  oName(nnewobj) = &system_names[ NAME_DollarError ] ;
  if ((errdict = fast_sys_extract_hash( &nnewobj )) == NULL)
    return FALSE ;

  /* look up /ErrorParams in $error */
  if ((errparams = fast_extract_hash_name(errdict, NAME_ErrorParams)) == NULL)
    return FALSE ;

  /* look up /errorinfo in /ErrorParams */
  if ((errinfo = fast_extract_hash_name(errparams, NAME_errorinfo)) == NULL)
    return FALSE ;

  if ( oType(*errinfo) != OARRAY && oType(*errinfo) != OPACKEDARRAY )
    return FALSE ;

  if (theLen(*errinfo) != 2) {
    HQFAIL("errinfo array does not have a length of two") ;
    return FALSE ;
  }

  *detail = oString(oArray(*errinfo)[1]) ;
  *detail_len = theLen(oArray(*errinfo)[1]) ;

  return TRUE ;
}

static void wcs_parse_error_cb(
      xmlGParser *xml_parser,
      hqn_uri_t *uri,
      uint32 line,
      uint32 column,
      uint8 *detail,
      int32  detail_len)
{
  uint8 *uri_name ;
  uint32 uri_name_len ;

  UNUSED_PARAM(xmlGParser*, xml_parser);
  UNUSED_PARAM(hqn_uri_t*, uri);

  HQASSERT(xml_parser != NULL, "xml_parser is NULL") ;

  if (wcscmm_error_occured)
    return ;
  wcscmm_error_occured = TRUE ;

  /* Although this should never happen, continue to report at least
     some error rather than no error at all. */
  if (! hqn_uri_get_field(uri, &uri_name, &uri_name_len, HQN_URI_NAME)) {
    uri_name = (uint8 *)"Invalid URI while handling error condition." ;
    uri_name_len = strlen_uint32((const char *)uri_name) ;
  }

  /* libgenxml internal errors may as well come out as UNDEFINED PS
     errors */
  if (detail == NULL || detail_len == 0) {
    (void)detailf_error_handler(UNDEFINED, SHORTFORM, uri_name_len, uri_name, line,
                                column) ;
  } else {
    (void)detailf_error_handler(UNDEFINED, LONGFORM, uri_name_len, uri_name, line,
                                column, detail_len, detail) ;
  }
}

/* This gets called if one of the xml_doc defined XML processing
   callbacks returns FALSE. The primary reason for distinguishing this
   error handler from the normal error handler is becuase this error
   handler will merely add location detail to the PS error
   message. Note that validity error callbacks are regarded as xml_doc
   defined XML processing callbacks, hence if the validity error
   callback returns FALSE, this function will also be called.  */
static void wcs_xml_doc_error_cb(
      xmlGFilter *filter,
      uint32 line,
      uint32 column)
{
  uint8 *detail ;
  int32 detail_len ;
  int32 errorno ;
  uint8 *uri ;
  uint32 uri_len ;

  HQASSERT(filter != NULL, "filter is NULL") ;

  if (wcscmm_error_occured)
    return ;
  wcscmm_error_occured = TRUE ;

  /* Although this should never happen, continue to report at least
     some error rather than no error at all. */
  if (! hqn_uri_get_field(xmlg_get_uri(filter),
                          &uri, &uri_len, HQN_URI_NAME)) {
    uri = (uint8 *)"Invalid URI while handling error condition." ;
    uri_len = strlen_uint32((const char *)uri) ;
  }

  /* Lookup PS error and get error number and detail. Then reformat
     with XML co-ordinates and reset the error. If a PS error has not
     been set, it means that an XML processing callback returned
     without setting an error - which is naughty, but we can catch
     this case will assert in debug build. */
  errorno = newerror ;
  if (errorno == FALSE) {
    HQFAIL("An XML processing callback has failed without raising a PS error.") ;
    errorno = UNDEFINED ;
    detail = (uint8 *)"Undefined error occured." ;
    detail_len = strlen_uint32((const char *)detail) ;
  } else {
    if (! get_error_detail(&detail, &detail_len)) {
      detail = (uint8 *)"No error detail provided." ;
      detail_len = strlen_uint32((const char *)detail) ;
    }
  }

  if (detail == NULL || detail_len == 0) {
    (void)detailf_error_handler(errorno, SHORTFORM, uri_len, uri, line,
                                column) ;
  } else {
    (void)detailf_error_handler(errorno, LONGFORM, uri_len, uri, line,
                                column, detail_len, detail) ;
  }
}

/* If this returns FALSE, parsing will abort immediately. */
static Bool vwcs_validity_error_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      int32 error_type,
      char *format,
      va_list vlist)
{
  Bool result ;
  int32 errorno ;

  UNUSED_PARAM( xmlGFilter* , filter ) ;
  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( xmlGAttributes* , attrs ) ;

  /* NOTE: that this error handler is called from different filters
     which have different contexts. */

  HQASSERT(!wcscmm_error_occured,
           "A validity error callback has been called after an error has been set.") ;
  if (wcscmm_error_occured) /* make it a safe assert */
    return FALSE ;

  errorno = SYNTAXERROR ;

  /* If its an invalid child error, create compatibility block so
     that we can look to see if the element is possibly ignorable. */
  if (error_type == XMLG_ERR_INVALID_CHILD ||
      error_type == XMLG_ERR_INVALID_DOC_ELEMENT) {

  } else if (error_type == XMLG_ERR_UNMATCHED_ATTRIBUTE) {
    /* NOTE: The element detail is the attribute. */

  } else if (error_type == XMLG_ERR_ATTRIBUTE_SCANERROR) {
    /* Keep convert callback PS error number. */
    if (newerror == FALSE) {
      HQFAIL("An XML match processing callback has failed without raising a PS error.") ;
      /* safe assert, default is set above. */
    } else {
      uint8 *unused_detail ;
      int32 unused_detail_len ;
      errorno = newerror ;

      /* If there is already detailed error information, we are done,
         otherwise drop through and use the default validation error
         messages from xmlg. */
      if (get_error_detail(&unused_detail, &unused_detail_len))
        return FALSE ;
    }
  }

  result = vdetailf_error_handler(errorno, format, vlist) ;

  return result ;
}

/* If this returns FALSE, parsing will abort immediately. */
static Bool wcs_validity_error_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      int32 error_type,
      char *format,
      ...)
{
  Bool result ;
  va_list vlist ;

  va_start( vlist, format ) ;
  result = vwcs_validity_error_cb(filter, localname, prefix, uri, attrs, error_type, format, vlist) ;
  va_end( vlist ) ;

  return result ;
}

Bool wcs_register_cb_array(
    xmlDocumentContext *xml_doc,
    xmlGFilter *filter,
    xmlGIStr *xml_namespace,
    wcsElementFuncts *funct_array)
{
  uint32 i;

  UNUSED_PARAM(xmlDocumentContext*, xml_doc) ;

  HQASSERT(xml_doc != NULL, "xml_doc is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(funct_array != NULL, "funct_array is NULL") ;

  for ( i = 0 ; funct_array[i].localname != NULL ; ++i ) {
    xmlGIStr *localname = funct_array[i].localname;

    if ( funct_array[i].f_start ) {
      if ( !xmlg_register_start_element_cb(filter, localname, xml_namespace,
                                           funct_array[i].f_start) )
        return error_handler(UNDEFINED) ;
    }
    if ( funct_array[i].f_end ) {
      if ( !xmlg_register_end_element_cb(filter, localname, xml_namespace,
                                         funct_array[i].f_end) )
        return error_handler(UNDEFINED) ;
    }
    if ( funct_array[i].f_chars ) {
      if ( !xmlg_register_characters_cb(filter, localname, xml_namespace,
                                        funct_array[i].f_chars) )
        return error_handler(UNDEFINED) ;
    }
  }

  return TRUE;
}

static int32 wcs_xml_decl_cb(
      xmlGFilter *filter,
      const uint8 *version,
      uint32 version_len,
      const uint8 *encoding,
      uint32 encoding_len,
      int32 standalone)
{
#define MAX_ENCODING_LEN 6
  uint8 encoding_lower[MAX_ENCODING_LEN] ;

  HQASSERT(version != NULL, "version is NULL") ;
  HQASSERT(standalone == -1 ||
           standalone == 0 ||
           standalone == 1, "standalone value unexpected") ;

  UNUSED_PARAM( xmlGFilter* , filter ) ;
  UNUSED_PARAM( int32 , standalone ) ;

  if (version == NULL || version_len != 3 ||
      (HqMemCmp(version, 3, UTF8_AND_LENGTH("1.0")) != 0)) {
    return detailf_error_handler(SYNTAXERROR,
           "Invalid XML version \"%.*s\". WCS profiles require the XML version to be 1.0.",
           version_len, version) ;
  }

  /* MUST be utf8 or utf16 */
  if (encoding != NULL) {
    Bool encoding_ok = FALSE ;

    if (encoding_len <= MAX_ENCODING_LEN) {
      uint32 i ;
      for (i=0; i<encoding_len; i++) {
        encoding_lower[i] = (uint8)tolower(encoding[i]) ;
      }

      switch (encoding_len) {
      case 5:
        encoding_ok = (HqMemCmp(encoding_lower, encoding_len, UTF8_AND_LENGTH("utf-8")) == 0) ;
        break ;
      case 6:
        encoding_ok = (HqMemCmp(encoding_lower, encoding_len, UTF8_AND_LENGTH("utf-16")) == 0) ;
        break ;
      default:
        encoding_ok = FALSE ;
      }
    }

    if (! encoding_ok)
      return detailf_error_handler(SYNTAXERROR,
             "Invalid XML encoding \"%.*s\". WCS profiles require the XML encoding to be either UTF-8 or UTF-16.",
             encoding_len, encoding) ;
  }

  return XMLG_RESULT_FORWARD ;
}

static Bool wcs_dtd_start_cb(
      xmlGFilter *filter,
      const uint8 *doctypeName,
      uint32 doctypeName_len,
      const uint8 *sysid,
      uint32 sysid_len,
      const uint8 *pubid,
      uint32 pubid_len,
      int32 has_internal_subset)
{
  UNUSED_PARAM( xmlGFilter* , filter ) ;
  UNUSED_PARAM( const uint8* , doctypeName ) ;
  UNUSED_PARAM( uint32 , doctypeName_len ) ;
  UNUSED_PARAM( const uint8* , sysid ) ;
  UNUSED_PARAM( uint32 , sysid_len ) ;
  UNUSED_PARAM( const uint8* , pubid ) ;
  UNUSED_PARAM( uint32 , pubid_len ) ;
  UNUSED_PARAM( int32 , has_internal_subset ) ;

  /* DTDs are not allowed in WCS */
  return detail_error_handler(SYNTAXERROR, "DTD is not allowed.") ;
}

static Bool wcs_fixed_payload_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      XMLG_VALID_CHILDREN *valid_children,
      xmlGIStr *xml_namespace,
      wcsElementFuncts *funct_array,
      xmlDocumentContext *xml_doc)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(xml_doc != NULL, "xml_doc is NULL") ;

  UNUSED_PARAM(XMLG_VALID_CHILDREN *, valid_children) ;

  *filter = NULL ;

  /* This filter has no cleanup. */
  if ( !xmlg_fc_new_filter(filter_chain, &new_filter, position, xml_doc,
                           NULL /* no cleanup callback */) )
    return error_handler(UNDEFINED) ;

  if ( !wcs_register_cb_array(xml_doc, new_filter,
                              XML_INTERN(ns_2005_02_wcs_cpt),
                              wcs_CommonProfileTypes_functs) ||
       !wcs_register_cb_array(xml_doc, new_filter, xml_namespace, funct_array) ) {
    xmlg_f_destroy(&new_filter) ;
    return FALSE ;
  }

  xmlg_set_validity_error_cb(new_filter, wcs_validity_error_cb) ;
  xmlg_set_user_error_cb(new_filter, wcs_xml_doc_error_cb) ;
  xmlg_set_xml_decl_cb(new_filter, wcs_xml_decl_cb) ;
  xmlg_set_dtd_start_cb(new_filter, wcs_dtd_start_cb) ;

  /* set valid document element */
  if ( !xmlg_valid_children(new_filter, NULL, NULL, valid_children) ) {
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  *filter = new_filter ;

  return TRUE ;
}

static Bool wcs_parse_profile(xmlDocumentContext *xml_doc, FILELIST *profile,
                              XMLG_VALID_CHILDREN *valid_children,
                              xmlGIStr *xml_namespace,
                              wcsElementFuncts *funct_array)
{
  Bool result = FALSE ;
  XMLExecContext *p_xmlexec_context = NULL ;
  xmlGFilterChain *filter_chain = NULL ;
  xmlGFilter *filter ;
  hqn_uri_t *uri = NULL ;

  if ( !xmlexec_context_create(&p_xmlexec_context) )
    goto cleanup ;

  /* A uri derived from a RSD or SFD filter is not much use, but
     I need something. */
  if ( !uri_from_open_filter(profile, &uri) )
    goto cleanup ;

  if ( !xmlg_fc_new(core_xml_subsystem, &filter_chain,
                    &xmlexec_memory_handlers,
                    uri, uri, NULL /* xml_doc data */) )
    goto cleanup ;

  if ( !wcs_fixed_payload_filter_init(filter_chain, 1 /* 0 is for the debug filter */,
                                      &filter, valid_children,
                                      xml_namespace, funct_array, xml_doc) )
    goto cleanup ;

  xmlg_fc_set_parse_error_cb(filter_chain, wcs_parse_error_cb) ;

  if ( !xml_parse_stream(profile, filter_chain) )
    goto cleanup ;

  result = TRUE ;
 cleanup:
  if ( filter_chain )
    xmlg_fc_destroy(&filter_chain) ;
  if ( uri )
    hqn_uri_free(&uri) ;
  if ( p_xmlexec_context )
    xmlexec_context_destroy(&p_xmlexec_context) ;
  if ( xml_doc->charsbuffer.unitlength > 0 ) {
    wcscmm_free(xml_doc->memstate, xml_doc->charsbuffer.codeunits) ;
    xml_doc->charsbuffer.codeunits = NULL ;
    xml_doc->charsbuffer.unitlength = 0 ;
  }

  return result ;
}

Bool wcs_parse_cdmp(sw_memory_instance *memstate, FILELIST *cdmp, wcsColorDeviceModel **cdm)
{
  xmlDocumentContext xml_doc = { 0 } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ColorDeviceModel), XML_INTERN(ns_2005_02_wcs_cdm), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  xml_doc.memstate = memstate ;
  xml_doc.modeltype = wcsColorDeviceModelType ;

  if ( !wcs_parse_profile(&xml_doc, cdmp, valid_children,
                          XML_INTERN(ns_2005_02_wcs_cdm),
                          wcs_ColorDeviceModel_functs) ) {
    wcs_destroy_cdm(memstate, xml_doc.model.cdm) ;
    wcscmm_error_occured = FALSE ;
    return FALSE ;
  }

  *cdm = xml_doc.model.cdm ;

  return TRUE ;
}

Bool wcs_parse_camp(sw_memory_instance *memstate, FILELIST *camp, wcsColorAppearanceModel **cam)
{
  xmlDocumentContext xml_doc = { 0 } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ColorAppearanceModel), XML_INTERN(ns_2005_02_wcs_cam), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  xml_doc.memstate = memstate ;
  xml_doc.modeltype = wcsColorAppearanceModelType ;

  if ( !wcs_parse_profile(&xml_doc, camp, valid_children,
                          XML_INTERN(ns_2005_02_wcs_cam),
                          wcs_ColorAppearanceModel_functs) ) {
    wcs_destroy_cam(memstate, xml_doc.model.cam) ;
    wcscmm_error_occured = FALSE ;
    return FALSE ;
  }

  *cam = xml_doc.model.cam ;

  return TRUE ;
}

Bool wcs_parse_gmmp(sw_memory_instance *memstate, FILELIST *gmmp, wcsGamutMapModel **gmm)
{
  xmlDocumentContext xml_doc = { 0 } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(GamutMapModel), XML_INTERN(ns_2005_02_wcs_gmm), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  xml_doc.memstate = memstate ;
  xml_doc.modeltype = wcsGamutMapModelType ;

  if ( !wcs_parse_profile(&xml_doc, gmmp, valid_children,
                          XML_INTERN(ns_2005_02_wcs_gmm),
                          wcs_GamutMapModel_functs) ) {
    wcs_destroy_gmm(memstate, xml_doc.model.gmm) ;
    wcscmm_error_occured = FALSE ;
    return FALSE ;
  }

  *gmm = xml_doc.model.gmm ;

  return TRUE ;
}

void init_C_globals_wcsxml(void)
{
  wcscmm_error_occured = FALSE ;
}

/*
* Log stripped */
