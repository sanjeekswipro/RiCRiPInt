/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:xps.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * XPS generic functions and callbacks.
 */

#include "core.h"
#include "coreinit.h"
#include "swstart.h"
#include "control.h"
#include "mm.h" /* mm_alloc */
#include "mmcompat.h" /* mm_alloc_with_header */
#include "lowmem.h" /* mm_memory_is_low */
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "gstack.h"          /* gstateptr */
#include "swctype.h"         /* tolower(), toupper() */
#include "swerrors.h"        /* error_handler() */
#include "objects.h"         /* OINTEGER, NAMECACHE */
#include "ripdebug.h"        /* register_ripvar */
#include "gs_color.h"        /* GSC_UNDEFINED */
#include "namedef_.h"        /* NAME_* */
#include "stacks.h"          /* operandstack */
#include "dictscan.h"        /* NAMETYPEMATCH */
#include "dicthash.h"
#include "gu_path.h"
#include "graphics.h"
#include "system.h"
#include "swcmm.h"           /* SW_INTENT_RELATIVE_COLORIMETRIC */
#include "xmltypeconv.h"
#include "monitor.h"

#include "xml.h"

#include "xpspriv.h"
#include "xps.h"
#include "xpsscan.h"
#include "xpsfonts.h"       /* xps_font_cache_init() */
#include "xpsiccbased.h"    /* xps_icc_cache_init() */
#include "xpspt.h"
#include "xpsresblock.h"
#include "xpspartspriv.h"
#include "fixedpage.h"
#include "package.h"
#include "xpsparams.h"
#include "obfont.h"

Bool xps_error_occurred = FALSE ;

/* debug_xps defined for assert build too, so we can HQTRACE on its value */
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
int32 debug_xps = 0 ;
#endif

/** \todo This is a hack and in the wrong place. */
extern Bool xps_versioning_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      struct xpsSupportedUri **table) ;

/* Hash table size which tracks content type of parts.
 * for xps's which contains many files.
 */
#define MIMETYPE_PARTNAME_HTSIZE 131u

/* Hash table size which tracks suffix (eg: .xml) default content types. */
#define MIMETYPE_EXTENSION_HTSIZE 131u

/* Hash table size which tracks URI's we support. */
#define SUPPORTEDURIS_HTSIZE 131u

/* Hash table size which tracks parts we have processed. This may be
 * too small for xps's which contains many files, but how big should
 * we make it?
 */
#define PROCESSED_PARTNAME_HTSIZE 2053u

struct xpsSupportedUri {
  const xmlGIStr *uri ;
  struct xpsSupportedUri *next ;
} ;

struct xpsProcessedPartName {
  const xmlGIStr *norm_name ;
  struct xpsProcessedPartName *next ;
} ;

static
struct xpsSupportedUri *find_uri(
      struct xpsSupportedUri **table,
      const xmlGIStr *uri,
      uint32 *hval)
{
  uintptr_t hash ;

  struct xpsSupportedUri *curr ;
  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(uri != NULL, "uri is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  /* Lets just use the hash value of the interned uri. */
  hash = intern_hash(uri) % SUPPORTEDURIS_HTSIZE ;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr = table[*hval]; curr != NULL; curr = curr->next)
    if (curr->uri == uri)
      return curr;

  return NULL;
}

static
Bool xps_add_supported_namespace(
      xmlDocumentContext *xps_ctxt,
      const xmlGIStr *uri)
{
  struct xpsSupportedUri *curr_uri ;
  uint32 hval ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(uri != NULL, "uri is NULL") ;

  if ((curr_uri = find_uri(xps_ctxt->supported_uris, uri, &hval)) == NULL) {
    if ((curr_uri = mm_alloc(mm_xml_pool, sizeof(struct xpsSupportedUri),
                             MM_ALLOC_CLASS_XPS_SUPPORTED_URI)) == NULL)
      return error_handler(VMERROR) ;

    curr_uri->uri = uri ;

    curr_uri->next = xps_ctxt->supported_uris[hval] ;
    xps_ctxt->supported_uris[hval] = curr_uri ;
  } /* else its already in the hash - we don't care */
  return TRUE ;
}

static
struct xpsProcessedPartName *find_processed_partname(
      struct xpsProcessedPartName **table,
      const xmlGIStr *norm_name,
      uint32 *hval)
{
  uintptr_t hash ;

  struct xpsProcessedPartName *curr ;
  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(norm_name != NULL, "norm_name is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  /* Lets just use the hash value of the interned uri. */
  hash = intern_hash(norm_name) % PROCESSED_PARTNAME_HTSIZE ;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr = table[*hval]; curr != NULL; curr = curr->next)
    if (curr->norm_name == norm_name)
      return curr;

  return NULL;
}

Bool xps_add_processed_partname(
      xmlDocumentContext *xps_ctxt,
      const xmlGIStr *norm_name)
{
  struct xpsProcessedPartName *curr_norm_name ;
  uint32 hval ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(norm_name != NULL, "norm_name is NULL") ;

  if ((curr_norm_name = find_processed_partname(xps_ctxt->parts_processed, norm_name, &hval)) == NULL) {
    if ((curr_norm_name = mm_alloc(mm_xml_pool, sizeof(struct xpsProcessedPartName),
                                   MM_ALLOC_CLASS_XPS_PROCESSED_PARTNAME)) == NULL)
      return error_handler(VMERROR) ;

    curr_norm_name->norm_name = norm_name ;

    curr_norm_name->next = xps_ctxt->parts_processed[hval] ;
    xps_ctxt->parts_processed[hval] = curr_norm_name ;
  } /* else its already in the hash - we don't care */
  return TRUE ;
}

Bool xps_is_processed_partname(
      xmlDocumentContext *xps_ctxt,
      const xmlGIStr *norm_name)
{
  uint32 hval ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(norm_name != NULL, "norm_name is NULL") ;

  return find_processed_partname(xps_ctxt->parts_processed, norm_name, &hval) != NULL ;
}

int32 xps_character_cb(
      xmlGFilter *filter,
      const uint8 *buf,
      uint32 buflen)
{
  uint32 i ;

  /* Intern up to the first whitespace */
  for ( i = 0 ; i < buflen ; ++i ) {
    UTF8 ch = (UTF8)buf[i] ;
    /* Probably not a good idea to include the character data in the
       error as it could be a rather large buffer. */
    if ( ! IS_XML_WHITESPACE(ch) ) {
      if (xmlg_get_element_depth(filter) == 0) {
        return detail_error_handler(UNDEFINED,
                                    "XML is not well-formed - non white-space character data found before the document element.") ;
      } else {
        return detail_error_handler(UNDEFINED,
                 "Arbitrary character data intermingled in the markup is not allowed.") ;
      }
    }
  }

  return XMLG_RESULT_FORWARD ;
}

int32 xps_core_properties_character_cb(
      xmlGFilter *filter,
      const uint8 *buf,
      uint32 buflen)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  uint32 i ;

  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt != NULL, "NULL xmlpart_ctxt") ;

  /* We do not allow character data outside of elements. */
  if (! xmlpart_ctxt->within_element) {
    /* Intern up to the first whitespace */
    for ( i = 0 ; i < buflen ; ++i ) {
      UTF8 ch = (UTF8)buf[i] ;
      /* Probably not a good idea to include the character data in the
         error as it could be a rather large buffer. */
      if ( ! IS_XML_WHITESPACE(ch) )
        return detail_error_handler(UNDEFINED,
                 "Arbitrary character data intermingled in the markup is not allowed.") ;
    }
  }

  return XMLG_RESULT_FORWARD ;
}

int32 xps_xml_decl_cb(
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
           "Invalid XML version \"%.*s\". XPS documents require the XML version to be 1.0.",
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
             "Invalid XML encoding \"%.*s\". XPS documents require the XML encoding to be either UTF-8 or UTF-16.",
             encoding_len, encoding) ;
  }

  return XMLG_RESULT_FORWARD ;
}

Bool xps_dtd_start_cb(
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

  /* DTDs are not allowed in fixedpayload */
  return detail_error_handler(SYNTAXERROR, "DTD is not allowed.") ;
}

Bool xps_fixed_payload_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      XMLG_VALID_CHILDREN *valid_children,
      xmlDocumentContext *xps_ctxt)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  *filter = NULL ;

  /* This filter has no cleanup. */
  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, xps_ctxt,
                           NULL /* no cleanup callback */))
    return error_handler(UNDEFINED) ;

  if (! xmlcb_register_functs_fixedpage(xps_ctxt, new_filter) ||
      ! xmlcb_register_functs_package(xps_ctxt, new_filter)) {
    xmlg_f_destroy(&new_filter) ;
    return FALSE ;
  }

  xmlg_set_validity_error_cb(new_filter, xps_validity_error_cb) ;
  xmlg_set_user_error_cb(new_filter, xps_user_error_cb) ;
  xmlg_set_character_cb(new_filter, xps_character_cb) ;
  xmlg_set_xml_decl_cb(new_filter, xps_xml_decl_cb) ;
  xmlg_set_dtd_start_cb(new_filter, xps_dtd_start_cb) ;

  /* set valid document element */
  if (! xmlg_valid_children(new_filter, NULL, NULL, valid_children)) {
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  *filter = new_filter ;

  return TRUE ;
}

Bool xps_core_properties_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      XMLG_VALID_CHILDREN *valid_children,
      xmlDocumentContext *xps_ctxt)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  *filter = NULL ;

  /* This filter has no cleanup. */
  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, xps_ctxt,
                           NULL /* no cleanup callback */))
    return error_handler(UNDEFINED) ;

  /* May as well only register the package callbacks. */
  if (! xmlcb_register_functs_package(xps_ctxt, new_filter)) {
    xmlg_f_destroy(&new_filter) ;
    return FALSE ;
  }

  xmlg_set_validity_error_cb(new_filter, xps_validity_error_cb) ;
  xmlg_set_user_error_cb(new_filter, xps_user_error_cb) ;
  xmlg_set_character_cb(new_filter, xps_core_properties_character_cb) ;
  xmlg_set_xml_decl_cb(new_filter, xps_xml_decl_cb) ;
  xmlg_set_dtd_start_cb(new_filter, xps_dtd_start_cb) ;

  /* set valid document element */
  if (! xmlg_valid_children(new_filter, NULL, NULL, valid_children)) {
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  *filter = new_filter ;

  return TRUE ;
}

Bool xps_is_supported_namespace(
      struct xpsSupportedUri **table,
      const xmlGIStr *uri)
{
  uint32 hval ;

  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(uri != NULL, "uri is NULL") ;

  return (find_uri(table, uri, &hval) != NULL) ;
}

/*=============================================================================
 * XPS error handlers
 *=============================================================================
 */

#define SHORTFORM ("PartName: %.*s; Line: %d; Column: %d.")
#define LONGFORM ("PartName: %.*s; Line: %d; Column: %d; XMLInfo: %.*s")

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

void xps_parse_error_cb(
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

  HQASSERT(xml_parser != NULL, "xml_parser is NULL") ;

  if (xps_error_occurred)
    return ;
  xps_error_occurred = TRUE ;

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

/* This gets called if one of the user defined XML processing
   callbacks returns FALSE. The primary reason for distinguishing this
   error handler from the normal error handler is because this error
   handler will merely add location detail to the PS error
   message. Note that validity error callbacks are regarded as user
   defined XML processing callbacks, hence if the validity error
   callback returns FALSE, this function will also be called.  */
void xps_user_error_cb(
      xmlGFilter *filter,
      uint32 line,
      uint32 column)
{
  uint8 *detail ;
  int32 detail_len ;
  int32 errorno ;
  uint8 *uri ;
  uint32 uri_len ;
  error_context_t *errcontext;

  HQASSERT(filter != NULL, "filter is NULL") ;

  if (xps_error_occurred)
    return ;
  xps_error_occurred = TRUE ;

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
  errcontext = get_core_context_interp()->error;
  errorno = errcontext->old_error;
  if (errorno == FALSE) {
    /* QA do not want this as an HQFAIL. */
    HQTRACE(TRUE, ("An XML processing callback has failed without raising a PS error.")) ;
    errorno = UNREGISTERED;
    detail = (uint8 *)"Undefined error occured." ;
    detail_len = strlen_uint32((const char *)detail) ;
  } else {
    if (! get_error_detail(&detail, &detail_len)) {
      detail = (uint8 *)"No error detail provided." ;
      detail_len = strlen_uint32((const char *)detail) ;
    }
    error_clear_context(errcontext); /* So the new details take effect. */
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
Bool vxps_validity_error_cb(
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

  HQASSERT(!xps_error_occurred,
           "A validity error callback has been called after an error has been set.") ;
  if (xps_error_occurred) /* make it a safe assert */
    return FALSE ;

  errorno = SYNTAXERROR ;

  /* If it's an invalid child error, create compatibility block so
     that we can look to see if the element is possibly ignorable. */
  if (error_type == XMLG_ERR_INVALID_CHILD ||
      error_type == XMLG_ERR_INVALID_DOC_ELEMENT) {

  } else if (error_type == XMLG_ERR_UNMATCHED_ATTRIBUTE) {
    /* NOTE: The element detail is the attribute. */

    /* Example of catching an attribute validation error. Key is now
       removed by the resource filter when immediately within a
       ResourceDictionary. When s0 filter finds a key attribute, we
       can report something a little more useful to the user in this
       instance. */
    if (localname == XML_INTERN(Key) && uri == XML_INTERN(ns_xps_2005_06_resourcedictionary_key)) {
      return detail_error_handler(SYNTAXERROR,
                                  "Key attribute defined on element which is not an immediate child of a ResourceDictionary.") ;
    }

  } else if (error_type == XMLG_ERR_ATTRIBUTE_SCANERROR) {
    /* Keep convert callback PS error number. */
    error_context_t *errcontext = get_core_context_interp()->error;

    if (errcontext->old_error == FALSE) {
      /* QA do not want this as an HQFAIL. */
      HQTRACE(TRUE, ("An XML match processing callback has failed without raising a PS error.")) ;
      /* safe assert, default is set above. */
    } else {
      uint8 *unused_detail ;
      int32 unused_detail_len ;

      /* If there is already detailed error information, we are done,
         otherwise drop through and use the default validation error
         messages from xmlg. */
      if (get_error_detail(&unused_detail, &unused_detail_len))
        return FALSE ;
      errorno = errcontext->old_error;
      error_clear_context(errcontext); /* So the new details take effect. */
    }
  }

  result = vdetailf_error_handler(errorno, format, vlist) ;
  return result ;
}


/* If this returns FALSE, parsing will abort immediately. */
Bool xps_validity_error_cb(
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
  result = vxps_validity_error_cb(filter, localname, prefix, uri, attrs, error_type, format, vlist) ;
  va_end( vlist ) ;

  return result ;
}

/*=============================================================================
 * Filters to handle low memory and user labels
 *=============================================================================
 */

static Bool lowmem_pre_start_element(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static Bool userlabel_set ;
  static Bool userlabel ;

  static XML_ATTRIBUTE_MATCH match[] = {
    /* GGS extensions */
    { XML_INTERN(UserLabel), XML_INTERN(ns_ggs_xps_2007_06), &userlabel_set, ggs_xps_convert_userlabel, &userlabel },
    XML_ATTRIBUTE_MATCH_END
  };

  UNUSED_PARAM( const xmlGIStr * , prefix ) ;

  HQASSERT((filter != NULL), "filter is NULL");
  HQASSERT((localname != NULL), "localname is NULL");

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE))
    return error_handler(UNDEFINED) ;

  if ( !userlabel_set) {
    userlabel = FALSE;
  } else {
    /* Remove the attribute otherwise the S0 filter will cause a
       validation error. */
    xmlg_attributes_remove(attrs, XML_INTERN(UserLabel),
                                  XML_INTERN(ns_ggs_xps_2007_06)) ;
  }

  /* We set the user label for all objects as it seems that in the
     nesting scenario, ths gstate user_label gets cleared. */
  ggs_xps_setuserlabel(filter, userlabel);

  /** \todo ++gc_safety_level should be conditional on the element. The old
      value should be stored here and restored after the operation. This code
      just turns off GC permanently till the end of XPS processing. */
  gc_safety_level = 2;

  return TRUE ;
}

static Bool lowmem_post_start_element(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  Bool status = TRUE ;

  UNUSED_PARAM( xmlGFilter * , filter ) ;
  UNUSED_PARAM( const xmlGIStr * , localname ) ;
  UNUSED_PARAM( const xmlGIStr * , uri ) ;
  UNUSED_PARAM( const xmlGIStr * , prefix ) ;
  UNUSED_PARAM( xmlGAttributes * , attrs ) ;

  /* Code for handling low memory, interrupts, timeouts, etc. */
  if (mm_memory_is_low || dosomeaction)
    status = handleNormalAction();

  return status ;
}

static Bool lowmem_pre_end_element(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  UNUSED_PARAM( xmlGFilter * , filter ) ;
  UNUSED_PARAM( const xmlGIStr * , localname ) ;
  UNUSED_PARAM( const xmlGIStr * , prefix ) ;
  UNUSED_PARAM( const xmlGIStr * , uri ) ;

  /** \todo ++gc_safety_level should be conditional on the element. The old
      value should be stored here and restored after the operation. This code
      just turns off GC permanently till the end of XPS processing. */
  gc_safety_level = 2;

  return success;
}


static Bool lowmem_post_end_element(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  HQASSERT((filter != NULL), "filter is NULL");

  UNUSED_PARAM( const xmlGIStr * , localname ) ;
  UNUSED_PARAM( const xmlGIStr * , prefix ) ;
  UNUSED_PARAM( const xmlGIStr * , uri ) ;

  ggs_xps_setuserlabel(filter, FALSE);

  /* Code for handling low memory, interrupts, timeouts, etc. */
  if (mm_memory_is_low || dosomeaction)
    success = success && handleNormalAction();
  return success;
}


Bool lowmem_pre_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;

  *filter = NULL ;

  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, NULL, NULL))
    return error_handler(UNDEFINED) ;

  /* watch all elements */
  if (! xmlg_register_end_element_cb(new_filter, NULL, NULL, /* all elements */
                                     lowmem_pre_end_element) ||
      ! xmlg_register_start_element_cb(new_filter, NULL, NULL, /* all elements */
                                       lowmem_pre_start_element)) {
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  *filter = new_filter ;

  return TRUE ;
}

Bool lowmem_post_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;

  *filter = NULL ;

  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, NULL, NULL))
    return error_handler(UNDEFINED) ;

  /* watch all elements */
  if (! xmlg_register_end_element_cb(new_filter, NULL, NULL, /* all elements */
                                     lowmem_post_end_element) ||
      ! xmlg_register_start_element_cb(new_filter, NULL, NULL, /* all elements */
                                       lowmem_post_start_element)) {
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  *filter = new_filter ;

  return TRUE ;
}

/*=============================================================================
 * Callback to create/destroy xps context and register appropriate callback
 * functions.
 *=============================================================================
 */

static void * xml_contenttype_malloc( size_t size)
{
  void *alloc;
  alloc = mm_alloc_with_header(mm_xml_pool, (mm_size_t)size,
                               MM_ALLOC_CLASS_XPS_CONTENTTYPE);
  if (alloc == NULL)
    (void) error_handler(VMERROR);

  return alloc;
}

static void xml_hash_free(void *memPtr)
{
  mm_free_with_header(mm_xml_pool, memPtr);
}

void xps_destroy_context(
      xmlDocumentContext **doc_context)
{
  xmlDocumentContext *xps_ctxt;
  uint32 i ;

  HQASSERT(doc_context != NULL, "doc_context is NULL") ;
  xps_ctxt = *doc_context ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  if (xps_ctxt->discard_parser != NULL)
    xps_close_discard_parser(&xps_ctxt->discard_parser) ;

  if (xps_ctxt->typestream_parser != NULL)
    xps_xml_close_typestream_parser(&(xps_ctxt->typestream_parser), xps_error_occurred) ;
  HQASSERT(xps_ctxt->typestream_parser == NULL, "type stream parser is NOT NULL") ;

  if (xps_ctxt->supported_uris != NULL) {
    for (i=0; i<SUPPORTEDURIS_HTSIZE; i++) {
      struct xpsSupportedUri *curr_uri, *next_uri ;
      for (curr_uri = xps_ctxt->supported_uris[i] ;
           curr_uri != NULL; curr_uri = next_uri) {
        next_uri = curr_uri->next ;
        mm_free(mm_xml_pool, curr_uri, sizeof(struct xpsSupportedUri)) ;
      }
    }
    mm_free(mm_xml_pool, xps_ctxt->supported_uris,
            sizeof(struct xpsSupportedUri*) * SUPPORTEDURIS_HTSIZE) ;
  }

  if (xps_ctxt->parts_processed != NULL) {
    for (i=0; i<PROCESSED_PARTNAME_HTSIZE; i++) {
      struct xpsProcessedPartName *curr_part, *next_part ;
      for (curr_part = xps_ctxt->parts_processed[i] ;
           curr_part != NULL; curr_part = next_part) {
        next_part = curr_part->next ;
        mm_free(mm_xml_pool, curr_part, sizeof(struct xpsProcessedPartName)) ;
      }
    }
    mm_free(mm_xml_pool, xps_ctxt->parts_processed,
            sizeof(struct xpsProcessedPartName*) * PROCESSED_PARTNAME_HTSIZE ) ;
  }

  path_free_list(thePath(xps_ctxt->path), mm_pool_temp) ;
  path_free_list(thePath(xps_ctxt->pathfill), mm_pool_temp) ;

  xps_ctxt->startpart_xmlpart_ctxt->flptr = NULL ;
  if (xps_ctxt->startpart_xmlpart_ctxt != NULL)
    xps_xml_part_context_free(&(xps_ctxt->startpart_xmlpart_ctxt)) ;
  if (xps_ctxt->startpart_partname != NULL)
    xps_partname_free(&(xps_ctxt->startpart_partname)) ;

  if (xps_ctxt->package_uri != NULL)
    hqn_uri_free(&xps_ctxt->package_uri) ;

  if (xps_ctxt->partname_to_mimetype != NULL)
    xmlstr_hash_destroy(&(xps_ctxt->partname_to_mimetype)) ;

  if (xps_ctxt->ext_to_mimetype != NULL)
    xmlstr_hash_destroy(&(xps_ctxt->ext_to_mimetype)) ;

  reset_direct_image(xps_ctxt) ;

  HQASSERT(xps_ctxt->callback_state == NULL,
           "Start/End pairing should guarantee callback state is empty now") ;

  mm_sac_destroy(mm_xml_pool) ;

  xps_partname_context_finish() ;

  mm_free(mm_xml_pool, *doc_context, sizeof(xmlDocumentContext)) ;

  xps_error_occurred = FALSE ;
  *doc_context = NULL ;
}

Bool xps_create_context(
      /*@null@*/ /*@in@*/
      OBJECT *params,
      /*@null@*/ /*@in@*/
      FILELIST *flptr,
      /*@notnull@*/ /*@out@*/
      xmlDocumentContext **doc_context,
      xmlGFilterChain *filter_chain)
{
  xmlGFilter *new_filter = NULL ;
  xmlDocumentContext *xps_ctxt;
  struct xpsXmlPartContext *new_xmlpart_ctxt = NULL ;
  xps_partname_t *new_part_name = NULL ;
  hqn_uri_t *base_uri = NULL ;
  xps_direct_image_t direct_image_init = { 0 } ;
  uint32 i ;

  static xmlDocumentContext EMPTY_CONTEXT = {
    FALSE,
  } ;

  static XMLG_VALID_CHILDREN relationships_doc_element[] = {
    { XML_INTERN(Relationships), XML_INTERN(ns_package_2006_relationships), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  enum {
    match_Strict,
    match_dummy
  } ;

  static NAMETYPEMATCH match[match_dummy + 1] = {
    { NAME_Strict | OOPTIONAL, 1, { OBOOLEAN } },
    DUMMY_END_MATCH
  } ;

  /* What do we think is a typical element depth for XPS jobs. */
#define EXPECTED_MAX_ELEMENT_DEPTH 30

  /* All SAC allocations should be 8 byte aligned. */
  struct mm_sac_classes_t sac_classes[] = { /* size, num, freq */
    { SAC_ALLOC_COMPAT_URI_ENTRY_SIZE, EXPECTED_MAX_ELEMENT_DEPTH, 10 },             /*    8 bytes - 20 Nov 2006 */
    { SAC_ALLOC_COMPAT_PROCESS_CONTENT_ENTRY_SIZE, EXPECTED_MAX_ELEMENT_DEPTH, 10 }, /*   16 bytes - 20 Nov 2006 */
    { SAC_ALLOC_COMPATBLOCK_SIZE, EXPECTED_MAX_ELEMENT_DEPTH, 10 },                  /*   40 bytes - 20 Nov 2006 */
    /* Do not expect more than 2 collisions per element - if at all. */
    { SAC_ALLOC_COMPLEX_PROPERTY_SIZE, (EXPECTED_MAX_ELEMENT_DEPTH * 2), 10 },       /*   48 bytes - 20 Nov 2006 */
    { SAC_ALLOC_COMMIT_BLOCK_SIZE, EXPECTED_MAX_ELEMENT_DEPTH, 10 }                  /*  184 bytes - 20 Nov 2006 (non-asserted),
                                                                                         192 bytes - 20 Nov 2006 (asserted) */
  } ;

  HQASSERT(doc_context != NULL, "doc_context is NULL") ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;

  *doc_context = NULL ;
  xps_error_occurred = FALSE ;

  if ( params != NULL && !dictmatch(params, match) )
    return FALSE ;

  if ( mm_sac_create(mm_xml_pool, sac_classes,
                     NUM_ARRAY_ITEMS(sac_classes)) != MM_SUCCESS )
    return error_handler(VMERROR);

  if ((xps_ctxt = mm_alloc(mm_xml_pool, sizeof(xmlDocumentContext),
                           MM_ALLOC_CLASS_XPS_CONTEXT)) == NULL)
    return error_handler(VMERROR);

  *xps_ctxt = EMPTY_CONTEXT ;

  xps_partname_context_init() ;

  if ((( xps_ctxt->ext_to_mimetype =
         xmlstr_hash_create(MIMETYPE_EXTENSION_HTSIZE, NULL /* payload free */,
               xml_contenttype_malloc, xml_hash_free, NULL /*default hash*/)) == NULL) ||
      (( xps_ctxt->partname_to_mimetype =
         xmlstr_hash_create(MIMETYPE_PARTNAME_HTSIZE, NULL /* payload free */,
               xml_contenttype_malloc, xml_hash_free, NULL /*default hash*/)) == NULL)) {
    goto error;
  }

  if ((xps_ctxt->supported_uris =
       mm_alloc(mm_xml_pool, sizeof(struct xpsSupportedUri*) * SUPPORTEDURIS_HTSIZE,
                MM_ALLOC_CLASS_XPS_SUPPORTED_URI)) == NULL) {
    (void)error_handler(VMERROR) ;
    goto error;
  }
  for (i=0; i<SUPPORTEDURIS_HTSIZE; i++) {
    xps_ctxt->supported_uris[i] = NULL ;
  }

  if ((xps_ctxt->parts_processed =
       mm_alloc(mm_xml_pool, sizeof(struct xpsProcessedPartName*) * PROCESSED_PARTNAME_HTSIZE,
                MM_ALLOC_CLASS_XPS_PROCESSED_PARTNAME)) == NULL) {
    (void)error_handler(VMERROR) ;
    goto error;
  }
  for (i=0; i<PROCESSED_PARTNAME_HTSIZE; i++) {
    xps_ctxt->parts_processed[i] = NULL ;
  }

  /* Initialise printticket handling */
  pt_init(&xps_ctxt->printticket) ;

  xps_ctxt->typestream_parser = NULL ;
  xps_ctxt->startpart_loaded = FALSE ;
  xps_ctxt->coreproperties_relationship_seen = FALSE ;
  xps_ctxt->resourceblock_uid = 0 ;
  xps_ctxt->fill_rule = EOFILL_TYPE ;
  xps_ctxt->colortype = GSC_UNDEFINED ;
  xps_ctxt->defaultRenderingIntentName = system_names + NAME_RelativeColorimetric ;
  xps_ctxt->capture_opacity = FALSE ;
  xps_ctxt->stroke_brush_opacity = 1.0 ;
  xps_ctxt->fill_brush_opacity = 1.0 ;
  xps_ctxt->ignore_isstroked = TRUE ;
  xps_ctxt->use_pathfill = FALSE ;
  path_init(&xps_ctxt->path) ;
  path_init(&xps_ctxt->pathfill) ;
  xps_ctxt->direct_image = direct_image_init;

  xps_ctxt->strict = TRUE ;

  if ( params != NULL && match[match_Strict].result )
    xps_ctxt->strict = oBool(*match[match_Strict].result) ;

  base_uri = xmlg_fc_get_base_uri(filter_chain) ;
  HQASSERT(base_uri != NULL, "base_uri is NULL") ;

  if (! hqn_uri_copy(core_uri_context, base_uri, &(xps_ctxt->package_uri) ) )
    goto error ;

  if (! xps_partname_new(&new_part_name, base_uri,
                         NAME_AND_LENGTH("/_rels/.rels"),
                         XPS_NORMALISE_PARTNAME))
    goto error ;

  /* Save for deallocation on context destroy. */
  xps_ctxt->startpart_partname = new_part_name ;

  /* Create new part context for _rels/.rels */
  if (! xps_xml_part_context_new(xps_ctxt, new_part_name,
                                 NULL, /* no relationships, so allocate them */
                                 XPS_PROCESS_COREPROPERTIES_REL, /* we are only interested in core properties */
                                 FALSE, /* no new relationships parser */
                                 &new_xmlpart_ctxt))
    goto error ;

  new_xmlpart_ctxt->flptr = flptr ;
  xps_ctxt->startpart_xmlpart_ctxt = new_xmlpart_ctxt ;

  xmlg_fc_set_user_data(filter_chain, new_xmlpart_ctxt) ;

  /* Override the parse error callback to be XPS specific. */
  xmlg_fc_set_parse_error_cb(filter_chain, xps_parse_error_cb) ;

  /* See request 45499. Supported namespaces are called understood
     namespaces in the latest spec. Since understood namespaces are
     only used for versioning and compatibility, we only add all the
     namespaces which are valid within parts which accept versioning
     and compatibility markup.

     This also raises the ugly question of the definition of
     "understood namespaces" and their scope. Are understood
     namespaces on a per package basis or per part basis?  We suspect
     per part.

     Currently we have a single understood namespace hash per
     package. Perhaps in the future it ought to be per
     part. None-the-less, everything still works OK in our
     implementation because of validation further down the filter
     chain. */
  if (! xps_add_supported_namespace(xps_ctxt, XML_INTERN(ns_w3_xml_namespace)) ||
      ! xps_add_supported_namespace(xps_ctxt, XML_INTERN(ns_markup_compatibility_2006)) ||
      ! xps_add_supported_namespace(xps_ctxt, XML_INTERN(ns_xps_2005_06)) ||
      ! xps_add_supported_namespace(xps_ctxt, XML_INTERN(ns_xps_2005_06_document_structure)) ||
      ! xps_add_supported_namespace(xps_ctxt, XML_INTERN(ns_xps_2005_06_resourcedictionary_key)) ||
      ! xps_add_supported_namespace(xps_ctxt, XML_INTERN(ns_xps_2005_06_signature_definitions)) ||
      ! xps_add_supported_namespace(xps_ctxt, XML_INTERN(ns_xps_2005_06_story_fragments)) ||
      ! xps_add_supported_namespace(xps_ctxt, XML_INTERN(ns_package_2006_relationships)))
    goto error ;

  if (! xps_versioning_filter_init(filter_chain, 3, &new_filter, xps_ctxt->supported_uris) ||
      ! xps_commit_filter_init(filter_chain, 5, &new_filter, xps_ctxt) ||
      ! xps_resource_filter_init(filter_chain, 7, &new_filter, xps_ctxt) ||
      ! xps_fixed_payload_filter_init(filter_chain, 10, &new_filter, relationships_doc_element, xps_ctxt))
    goto error ;

  xps_ctxt->typestream_parser = xps_xml_open_typestream_parser(xps_ctxt, xps_ctxt->package_uri) ;
  if (xps_ctxt->typestream_parser == NULL)
    goto error ;

  *doc_context = xps_ctxt ;

  return TRUE ;

 error:
  /* We MUST remove the XPS processing filter otherwise the XPS error
     handler will be invoked too many times. */
  if (new_filter != NULL)
    xmlg_f_destroy(&new_filter) ;

  xps_destroy_context(&xps_ctxt) ;

  return FALSE ;
}

/*=============================================================================
 * xps function registration entry point and utility
 *=============================================================================
 */

Bool xmlg_register_cb_array(
    xmlGFilter *filter,
    xmlGIStr *xml_namespace,
    xpsElementFuncts *funct_array)
{
  uint32 i;

  HQASSERT(funct_array != NULL, "funct_array is NULL") ;

  for (i=0; funct_array[i].localname != NULL; i++) {
    if (funct_array[i].f_start != NULL || funct_array[i].f_end != NULL) {
      xmlGIStr *localname = funct_array[i].localname;

      if (funct_array[i].f_start != NULL) {
        if (! xmlg_register_start_element_cb(filter, localname, xml_namespace,
                    funct_array[i].f_start)) {
          return FALSE;
        }
      }
      if (funct_array[i].f_end != NULL) {
        if (! xmlg_register_end_element_cb(filter, localname, xml_namespace,
                    funct_array[i].f_end)) {
          return FALSE;
        }
      }
      if (funct_array[i].f_chars != NULL) {
        if (! xmlg_register_characters_cb(filter, localname, xml_namespace,
                    funct_array[i].f_chars)) {
          return FALSE;
        }
      }
    }
  }

  return TRUE;
}

Bool xps_register_cb_array(
    xmlDocumentContext *xps_ctxt,
    xmlGFilter *filter,
    xmlGIStr *xml_namespace,
    xpsElementFuncts *funct_array)
{
  uint32 i;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(funct_array != NULL, "funct_array is NULL") ;

  for (i=0; funct_array[i].localname != NULL; i++) {
    if (funct_array[i].f_start != NULL || funct_array[i].f_end != NULL) {
      xmlGIStr *localname = funct_array[i].localname;

      if (funct_array[i].f_start != NULL) {
        if (! xmlg_register_start_element_cb(filter, localname, xml_namespace,
                    funct_array[i].f_start)) {
          return error_handler(UNDEFINED);
        }
      }
      if (funct_array[i].f_end != NULL) {
        if (! xmlg_register_end_element_cb(filter, localname, xml_namespace,
                    funct_array[i].f_end)) {
          return error_handler(UNDEFINED);
        }
      }
      if (funct_array[i].f_chars != NULL) {
        if (! xmlg_register_characters_cb(filter, localname, xml_namespace,
                    funct_array[i].f_chars)) {
          return error_handler(UNDEFINED);
        }
      }
    }
    if (! xps_add_supported_namespace(xps_ctxt, xml_namespace))
      return FALSE ;
  }

  return TRUE;
}

/** Initialisation sub-table for XPS. Some of the XPS routines create GC
    roots which need destroying. */
static core_init_t xps_init[] = {
  CORE_INIT_LOCAL("XPS typestream", NULL, xps_typestream_swstart, NULL,
                  xps_typestream_finish),
  CORE_INIT_LOCAL("XPS font cache", NULL, xps_font_cache_swstart, NULL,
                  xps_font_cache_finish),
  CORE_INIT_LOCAL("XPS colorspaces", NULL, xps_colorspace_swstart, NULL,
                  xps_colorspace_finish),
  CORE_INIT_LOCAL("XPS XML params", NULL, xps_xml_params_swstart, NULL,
                  xps_xml_params_finish),
} ;

static Bool xps_swinit(SWSTART *params)
{
  return core_swinit_run(xps_init, NUM_ARRAY_ITEMS(xps_init), params) ;
}

static Bool xps_swstart(SWSTART *params)
{
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  register_ripvar(NAME_debug_xps, OINTEGER, &debug_xps);
#endif

  if (! namespace_recognition_add(XML_INTERN(ns_package_2006_relationships),
                                  xps_create_context,
                                  xps_destroy_context))
    return FALSE;

  return core_swstart_run(xps_init, NUM_ARRAY_ITEMS(xps_init), params) ;
}

static Bool xps_postboot(void)
{
  return core_postboot_run(xps_init, NUM_ARRAY_ITEMS(xps_init)) ;
}

static void xps_finish(void)
{
  core_finish_run(xps_init, NUM_ARRAY_ITEMS(xps_init)) ;
}


/** \brief Operator to get back into XPS world from PostScript pattern
    PaintProc. */
Bool xpsbrush_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  utf8_buffer xmlname ;
  xmlGFilterChain *filter_chain ;
  int32 filter_chain_id ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Stack contains filter chain id. The filter chain is found and
     then XML from the cache executed on it. */
  if ( theStackSize(operandstack) < 1 )
    return error_handler(STACKUNDERFLOW) ;

  theo = theTop(operandstack);
  switch ( oType(*theo) ) {
  case OSTRING:
    xmlname.codeunits = oString(*theo) ;
    xmlname.unitlength = theLen(*theo) ;
    break ;
  case ONAME:
    xmlname.codeunits = theICList(oName(*theo)) ;
    xmlname.unitlength = theINLen(oName(*theo)) ;
    break ;
  default:
    return error_handler(TYPECHECK) ;
  }

  --theo ;
  if ( !fastStackAccess(operandstack) )
    theo = stackindex(1, &operandstack);

  if ( oType(*theo) != OINTEGER )
    return error_handler(TYPECHECK) ;

  filter_chain_id = oInteger(*theo);

  /* Lookup filter chain by id. */
  filter_chain = xmlg_find_filter_chain(filter_chain_id) ;
  if ( filter_chain == NULL )
    return error_handler(UNDEFINED) ;

  if (! xps_brush_execute(filter_chain, &xmlname) )
    return FALSE ;

  npop(2, &operandstack) ;

  return TRUE ;
}

void ggs_xps_setuserlabel(
    xmlGFilter *filter,
    Bool value)
{
  xpsXmlPartContext *xmlpart_ctxt ;
  xmlGFilterChain *filter_chain ;
  uint32 depth ;

  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "NULL filter_chain") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt != NULL, "NULL xmlpart_ctxt") ;

  depth = xmlg_get_element_depth(filter) ;

  /* If the user labels have not been turned on. */
  if (xmlpart_ctxt->outermost_userlabel_depth == 0) {

    if (value) { /* If turning on. */
      xmlpart_ctxt->outermost_userlabel_depth = depth ;
      gstateptr->user_label = (uint8)TRUE ;
    } else {
      /* Ensure user labels remain off within the gstate. */
      gstateptr->user_label = (uint8)FALSE ;
    }

  } else {
    /* Ensure user labels remain on within the gstate. */
    gstateptr->user_label = (uint8)TRUE ;

    /* If turning off && the depth is the same, turn off user
       labels. */
    if (! value && xmlpart_ctxt->outermost_userlabel_depth == depth) {
      xmlpart_ctxt->outermost_userlabel_depth = 0 ;
      gstateptr->user_label = (uint8)FALSE ;
    }
  }
}

static void init_C_globals_xps(void)
{
  xps_error_occurred = FALSE ;
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  debug_xps = 0 ;
#endif
}

IMPORT_INIT_C_GLOBALS( coreproperties )
IMPORT_INIT_C_GLOBALS( obfont )
IMPORT_INIT_C_GLOBALS( parts )
IMPORT_INIT_C_GLOBALS( typestream )
IMPORT_INIT_C_GLOBALS( xpsfonts )
IMPORT_INIT_C_GLOBALS( xpsiccbased )
IMPORT_INIT_C_GLOBALS( xpsparams )

/** Compound runtime initialisation */
void xps_C_globals(core_init_fns *fns)
{
  init_C_globals_coreproperties() ;
  init_C_globals_obfont() ;
  init_C_globals_parts() ;
  init_C_globals_typestream() ;
  init_C_globals_xps() ;
  init_C_globals_xpsfonts() ;
  init_C_globals_xpsiccbased() ;
  init_C_globals_xpsparams() ;

  fns->swinit = xps_swinit ;
  fns->swstart = xps_swstart ;
  fns->postboot = xps_postboot ;
  fns->finish = xps_finish ;

  core_C_globals_run(xps_init, NUM_ARRAY_ITEMS(xps_init)) ;
}

/* ============================================================================
* Log stripped */
