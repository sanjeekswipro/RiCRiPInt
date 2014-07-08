/* ============================================================================
 * $HopeName: HQNgenericxml!src:gexpat.c(EBDSDK_P.1) $
 * $Id: src:gexpat.c,v 1.48.10.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Expat back end to generic XML parse interface.
 */

#include "hqnuri.h"
#include "xmlgattrspriv.h"
#include "xmlgfunctablepriv.h"
#include "gexpat.h"

/* The character which separates fields within expat. */
static const XML_Char sep = '|';

/**
 * Private function to obtain the common part of the XML parse structure.
 */
xmlGParserCommon* xmlg_get_parser_common(
      xmlGParser *xml_parser)
{
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  return &(xml_parser->c);
}

void xmlg_parser_error_abort(
      xmlGParser *xml_parser,
      HqBool fire_error_handler,
      uint8 *detail,
      int32  detail_len)
{
  xmlGParserCommon *c ;
  xmlGFilterChain *filter_chain ;
  uint32 line, column ;

  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL") ;
  c = xmlg_get_parser_common(xml_parser);
  XMLGASSERT(c != NULL, "common is NULL");

  filter_chain = c->filter_chain ;

  /* setting the callback functions to NULL ensures we stop */
  XML_SetElementHandler(xml_parser->ctxt, NULL, NULL) ;
  XML_SetStartNamespaceDeclHandler(xml_parser->ctxt, NULL) ;
  XML_SetCharacterDataHandler(xml_parser->ctxt, NULL) ;
  XML_SetXmlDeclHandler(xml_parser->ctxt, NULL) ;
  XML_SetStartDoctypeDeclHandler(xml_parser->ctxt, NULL) ;

  if (fire_error_handler) {
    xmlg_parser_line_and_column(xml_parser, &line, &column) ;

    /* only ever invoke the error handler once */
    if (c->parse_error_cb != NULL && fire_error_handler && ! c->success_abort)
      c->parse_error_cb(xml_parser, xmlg_fc_get_uri(filter_chain), line, column,
                        detail, detail_len) ;
  }

  if (! c->success_abort)
    c->error_abort = TRUE ;
}

/* ============================================================================
 * Native expat callbacks.
 */
static
void XMLCALL expat_start_namespace_cb(
      void *data,
      const char *prefix,
      const char *uri)
{
  xmlGParser *xml_parser;
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  xmlGParserCommon *c ;
  const xmlGIStr *istr_prefix, *istr_uri, *istr_alias_uri ;
  uint32 prefixlen, urilen ;


  XMLGASSERT(data != NULL, "data is NULL");
  xml_parser = data;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");

  c = xmlg_get_parser_common(xml_parser);
  xml_ctxt = c->xml_ctxt;
  filter_chain = c->filter_chain ;

  /* If there is no filter chain, there is nothing to do. */
  if (filter_chain == NULL)
    return ;

  prefixlen = 0 ;
  urilen = 0 ;
  istr_prefix = NULL ;
  istr_uri = NULL ;

  if (prefix != NULL) {
    prefixlen = strlen_uint32(prefix) ;
    if (! xmlg_istring_create(xml_ctxt, &istr_prefix, (uint8 *)prefix, prefixlen)) {
      goto error ;
    }
  }
  if (uri != NULL) {
    urilen = strlen_uint32(uri) ;
    if (! xmlg_istring_create(xml_ctxt, &istr_alias_uri, (uint8 *)uri, urilen)) {
      goto error ;
    }

    if (c->mapuri_cb == NULL) {
      istr_uri = istr_alias_uri ;
    } else {
      if (! c->mapuri_cb(istr_alias_uri, &istr_uri)) {
        goto error ;
      }
    }
    HQASSERT(istr_uri != NULL, "istr_uri pointer is NULL") ;
  }

  if (! xmlg_fc_execute_namespace(filter_chain, istr_prefix, istr_uri)) {
    xmlg_parser_error_abort(xml_parser, FALSE, NULL, 0) ;
  }

  goto done ;

 error:
  xmlg_parser_error_abort(xml_parser, TRUE, NULL, 0) ;

 done:
  /* Destroy the strings. */
  xmlg_istring_destroy(xml_ctxt, &istr_prefix) ;
  xmlg_istring_destroy(xml_ctxt, &istr_uri) ;

  return;
}

static
HqBool expat_intern_names(
      xmlGParser *xml_parser,
      xmlGContext *xml_ctxt,
      char *uri,
      const xmlGIStr **istr_localname,
      const xmlGIStr **istr_prefix,
      const xmlGIStr **istr_uri)
{
  char *localname, *prefix, *name_sep, *prefix_sep, *ptr ;
  uint32 istrlen_localname, istrlen_prefix, istrlen_uri ;
  const xmlGIStr *istr_alias_uri ;
  xmlGParserCommon *c ;

  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL") ;
  c = xmlg_get_parser_common(xml_parser);
  XMLGASSERT(uri != NULL, "uri is NULL") ;
  XMLGASSERT(istr_localname != NULL, "istr_localname is NULL") ;
  XMLGASSERT(istr_prefix != NULL, "istr_prefix is NULL") ;
  XMLGASSERT(istr_uri != NULL, "istr_uri is NULL") ;

  /* Expat strings. */
  localname = uri ;
  prefix = NULL ;
  name_sep = NULL ;
  prefix_sep = NULL ;

  /* xmlGIStr structures. */
  *istr_localname = NULL ;
  *istr_prefix = NULL ;
  *istr_uri = NULL ;
  istrlen_localname = 0 ;
  istrlen_prefix = 0 ;
  istrlen_uri = 0 ;

  /* Look to see if we have a URI. */
  while (*localname != '\0' && *localname != sep) {
    localname++ ;
  }

  /* Note: we are not counting the NUL character as part of the length. */
  if (*localname == sep) {
    /* We will put this back as it was after the function call. We are
     * effectively trampling on someone elses memory.
     */
    name_sep = localname ;
    istrlen_uri = CAST_PTRDIFFT_TO_UINT32(localname - uri) ;
    *localname++ = '\0' ;

    /* Scan looking for a potential prefix or end of string. */
    ptr = localname;
    while (*ptr != '\0' && *ptr != sep) {
      ptr++ ;
    }
    istrlen_localname = CAST_PTRDIFFT_TO_UINT32(ptr - localname) ;

    /* We have a prefix. */
    if (*ptr == sep) {
      prefix_sep = ptr ;
      *ptr++ = '\0' ;
      prefix = ptr ;
      /* Scan to end to find length of prefix. */
      while (*ptr != '\0') {
        ptr++ ;
      }
      istrlen_prefix = CAST_PTRDIFFT_TO_UINT32(ptr - prefix) ;
    }

  } else if (*localname == '\0') {
    istrlen_localname = CAST_PTRDIFFT_TO_UINT32(localname - uri) ;
    localname = uri ;
    istrlen_uri = 0 ;
    uri = NULL ;
  }

  if (localname != NULL) {
    if (! xmlg_istring_create(xml_ctxt, istr_localname,
                              (uint8 *)localname, istrlen_localname)) {
      if (name_sep != NULL) *name_sep = sep ;
      if (prefix_sep != NULL) *prefix_sep = sep ;
      return FALSE ;
    }
  }

  if (prefix != NULL) {
    if (! xmlg_istring_create(xml_ctxt, istr_prefix,
                             (uint8 *)prefix, istrlen_prefix)) {
      if (name_sep != NULL) *name_sep = sep ;
      if (prefix_sep != NULL) *prefix_sep = sep ;
      return FALSE ;
    }
  }

  if (uri != NULL) {
    if (! xmlg_istring_create(xml_ctxt, &istr_alias_uri,
                              (uint8 *)uri, istrlen_uri)) {
      if (name_sep != NULL) *name_sep = sep ;
      if (prefix_sep != NULL) *prefix_sep = sep ;
      return FALSE ;
    }

    if (c->mapuri_cb == NULL) {
      *istr_uri = istr_alias_uri ;
    } else {
      if (! c->mapuri_cb(istr_alias_uri, istr_uri)) {
        if (name_sep != NULL) *name_sep = sep ;
        if (prefix_sep != NULL) *prefix_sep = sep ;
        return FALSE ;
      }
    }
    HQASSERT(*istr_uri != NULL, "istr_uri pointer is NULL") ;

    if (c->mapuri_cb != NULL &&
        ! c->mapuri_cb(istr_alias_uri, istr_uri)) {
      if (name_sep != NULL) *name_sep = sep ;
      if (prefix_sep != NULL) *prefix_sep = sep ;
      return FALSE ;
    }
    HQASSERT(*istr_uri != NULL, "istr_uri pointer is NULL") ;
  }

  /* Restore the expat string. */
  if (name_sep != NULL) *name_sep = sep ;
  if (prefix_sep != NULL) *prefix_sep = sep ;
  return TRUE ;
}

static
void XMLCALL expat_start_element_ns_cb(
      void *data,
      const char *uri,
      const char **attributes)
{
  const xmlGIStr *istr_localname, *istr_prefix, *istr_uri,
    *istr_attrlocalname, *istr_attrprefix, *istr_attruri ;
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  xmlGParser *xml_parser ;
  xmlGParserCommon *c ;
  xmlGAttributes *attr_hash ;
  const char *attr_value, *ptr;
  uint32 attr_value_len, i;

  XMLGASSERT(data != NULL, "data is NULL");
  XMLGASSERT(uri != NULL, "uri is NULL");
  xml_parser = data;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");

  attr_hash = NULL;

  c = xmlg_get_parser_common(xml_parser);
  xml_ctxt = c->xml_ctxt;
  filter_chain = c->filter_chain ;

  /* If there is no filter chain, there is nothing to do. */
  if (filter_chain == NULL)
    return ;

  if (! expat_intern_names(xml_parser, xml_ctxt, (char *)uri,
                           &istr_localname, &istr_prefix, &istr_uri)) {
    goto error;
  }

  if (attributes != NULL && attributes[0]) {
    if (! xmlg_attributes_create(filter_chain, &attr_hash)) {
      goto error;
    }

    for (i=0; attributes[i]; i+=2) {
      if (! expat_intern_names(xml_parser, xml_ctxt, (char *)attributes[i],
                               &istr_attrlocalname, &istr_attrprefix, &istr_attruri)) {
        /* Destroy the 3 strings. */
        xmlg_istring_destroy(xml_ctxt, &istr_localname);
        xmlg_istring_destroy(xml_ctxt, &istr_prefix);
        xmlg_istring_destroy(xml_ctxt, &istr_uri);

        /* Note that the partially filled attribute hash free's its payload via
         * a callback to xmlGStringDestroy.
         */
        goto error;
      }

      /* Get the length of the attribute value. */
      attr_value = attributes[i + 1];
      ptr = attr_value;
      attr_value_len = 0;
      while (*ptr++ != '\0') attr_value_len++;
      if (attr_value_len == 0) attr_value = NULL;

      /* Attributes insert takes a copy of the attribute name and URI, but not
       * the value.
       */
      if (!xmlg_attributes_insert(attr_hash,
                                  istr_attrlocalname,
                                  istr_attrprefix,
                                  istr_attruri,
                                  (uint8*)attr_value,
                                  attr_value_len)) {
        goto error;
      }
    }
  }

  if (! xmlg_fc_execute_start_element(filter_chain, istr_localname,
                                      istr_prefix, istr_uri, attr_hash)) {
    xmlg_parser_error_abort(xml_parser, FALSE, NULL, 0) ;
  }
  goto done;

 error:
  xmlg_parser_error_abort(xml_parser, TRUE, NULL, 0) ;

 done:
  /* Destroy the strings. */
  xmlg_istring_destroy(xml_ctxt, &istr_localname);
  xmlg_istring_destroy(xml_ctxt, &istr_prefix);
  xmlg_istring_destroy(xml_ctxt, &istr_uri);
  if (attr_hash != NULL) {
    xmlg_attributes_destroy(&attr_hash);
  }
  return;
}

static
void XMLCALL expat_end_element_ns_cb(
      void *data,
      const char *uri)
{
  const xmlGIStr *istr_localname, *istr_prefix, *istr_uri;
  xmlGParser *xml_parser;
  xmlGContext *xml_ctxt;
  xmlGFilterChain *filter_chain ;
  xmlGParserCommon *c;
  XMLGASSERT(data != NULL, "data is NULL");
  XMLGASSERT(uri != NULL, "uri is NULL");
  xml_parser = data;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");

  c = xmlg_get_parser_common(xml_parser);
  xml_ctxt = c->xml_ctxt;
  filter_chain = c->filter_chain ;

  /* If there is no filter chain, there is nothing to do. */
  if (filter_chain == NULL)
    return ;

  if (! expat_intern_names(xml_parser, xml_ctxt, (char *)uri,
                           &istr_localname, &istr_prefix, &istr_uri)) {
    goto error;
  }

  if (! xmlg_fc_execute_end_element(filter_chain, istr_localname, istr_prefix,
                                    istr_uri, TRUE)) {
    xmlg_parser_error_abort(xml_parser, FALSE, NULL, 0) ;
  }
  goto done;

 error:
  xmlg_parser_error_abort(xml_parser, TRUE, NULL, 0) ;

 done:
  /* Destroy the strings. */
  xmlg_istring_destroy(xml_ctxt, &istr_localname);
  xmlg_istring_destroy(xml_ctxt, &istr_prefix);
  xmlg_istring_destroy(xml_ctxt, &istr_uri);

  return;
}

static void XMLCALL expat_characters_data_cb(
      void *data,
      const char *s,
      int len)
{
  xmlGParser *xml_parser;
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  xmlGParserCommon *c ;

  XMLGASSERT(data != NULL, "data is NULL");
  xml_parser = data;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");

  c = xmlg_get_parser_common(xml_parser);
  xml_ctxt = c->xml_ctxt;
  filter_chain = c->filter_chain ;

  /* If there is no filter chain, there is nothing to do. */
  if (filter_chain == NULL)
    return ;

  if (! xmlg_fc_execute_characters(filter_chain,
                                   (const unsigned char *)s, (unsigned int)len)) {
    xmlg_parser_error_abort(xml_parser, FALSE, NULL, 0) ;
  }
}

static void XMLCALL expat_xml_decl_cb(
      void *data,
      const char *v,
      const char *e,
      int s)
{
  xmlGParser *xml_parser;
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  xmlGParserCommon *c ;
  unsigned int v_len = 0, e_len = 0 ;

  XMLGASSERT(data != NULL, "data is NULL");
  xml_parser = data;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");

  c = xmlg_get_parser_common(xml_parser);
  xml_ctxt = c->xml_ctxt;
  filter_chain = c->filter_chain ;

  /* If there is no filter chain, there is nothing to do. */
  if (filter_chain == NULL)
    return ;

  /* If v is NULL, its a text declaration according to expat
     documentation. */
  if (v == NULL)
    return ;

  v_len = strlen_uint32(v) ;
  if (e != NULL)
    e_len = strlen_uint32(e) ;

  if (! xmlg_fc_execute_xml_decl(filter_chain,
                    (const unsigned char *)v, v_len,
                    (const unsigned char *)e, e_len, s)) {
    xmlg_parser_error_abort(xml_parser, FALSE, NULL, 0) ;
  }
}

static void XMLCALL expat_xml_dtd_cb(
      void  *data,
      const char *n,
      const char *s,
      const char *p,
      int h)
{
  xmlGParser *xml_parser;
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  xmlGParserCommon *c ;
  unsigned int n_len, s_len = 0, p_len = 0 ;

  XMLGASSERT(data != NULL, "data is NULL");
  xml_parser = data;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");

  c = xmlg_get_parser_common(xml_parser);
  xml_ctxt = c->xml_ctxt;
  filter_chain = c->filter_chain ;

  /* If there is no filter chain, there is nothing to do. */
  if (filter_chain == NULL)
    return ;

  n_len = strlen_uint32(n);
  if (s != NULL)
    s_len = strlen_uint32(s);
  if (p != NULL)
    p_len = strlen_uint32(p);

  if (! xmlg_fc_execute_start_dtd(filter_chain,
                                (const unsigned char *)n, n_len,
                                (const unsigned char *)s, s_len,
                                (const unsigned char *)p, p_len,
                                h)) {
    xmlg_parser_error_abort(xml_parser, FALSE, NULL, 0) ;
  }
}

/* ============================================================================
 * Initialize and Terminate the XML sub system.
 */
HqBool xmlg_initialize(
      xmlGContext **xml_ctxt,
      mps_pool_t pool,
      xmlGMemoryHandler *memory_handler,
      xmlGIStrHandler *intern_handler,
      hqn_uri_context_t *uri_context)
{
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

  return xmlg_subsystem_common_init(xml_ctxt,
                                    pool,
                                    memory_handler,
                                    intern_handler,
                                    uri_context) ;
}

void xmlg_terminate(
      xmlGContext **xml_ctxt)
{
  XMLGASSERT(xml_ctxt != NULL,
             "xml_ctxt is NULL") ;
  XMLGASSERT(*xml_ctxt != NULL,
             "xml_ctxt pointer is NULL") ;

  xmlg_subsystem_common_terminate(xml_ctxt) ;
}


/* ============================================================================
 * Create/Destroy XML parse handler.
 */
HqBool xmlg_p_new(
      xmlGContext *xml_ctxt,
      xmlGParser **xml_parser,
      xmlGMemoryHandler *memory_handler,
      xmlGFilterChain *filter_chain)
{
  xmlGParser *new_xml_parser;

  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL");
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  *xml_parser = NULL;

  /* Although this code is going to be common to all back end XML parsers,
     we need to do it here because only this file knows about the size of
     xmlGParser - which is back end specific. */
  if (memory_handler != NULL) {
    if (memory_handler->f_malloc == NULL ||
        memory_handler->f_realloc == NULL ||
        memory_handler->f_free == NULL) {

      XMLGASSERT(FALSE, "incomplete memory handler") ;
      return FALSE ;
    }

    /* Use the pluged memory handler to allocate this structure */
    if ((new_xml_parser = memory_handler->f_malloc(sizeof(xmlGParser))) == NULL)
      return FALSE;
  } else {
    /* use the XML sub-system memory management */
    if ((new_xml_parser = xmlg_subsystem_malloc(xml_ctxt, sizeof(xmlGParser))) == NULL)
      return FALSE;
  }

  if (! xmlg_parser_common_init(new_xml_parser,
                                xml_ctxt,
                                memory_handler,
                                filter_chain)) {
    /* We need to do this as we don't know if the memory handlers have
       been plugged successfuly. Better safe than sorry. */
    if (memory_handler != NULL) {
      memory_handler->f_free(new_xml_parser);
    } else {
      xmlg_subsystem_free(xml_ctxt, new_xml_parser);
    }
    return FALSE;
  }
  /* xmlg_parser_malloc and friends are now available for this
     handler. */

  /* Do backend XML parser specific stuff. */

  /* setup expat memory handler structure */
  if (memory_handler != NULL) {
    new_xml_parser->expat_mem.malloc_fcn = memory_handler->f_malloc ;
    new_xml_parser->expat_mem.realloc_fcn = memory_handler->f_realloc ;
    new_xml_parser->expat_mem.free_fcn = memory_handler->f_free ;
  } else {
    new_xml_parser->expat_mem.malloc_fcn = xml_ctxt->memory_handler.f_malloc ;
    new_xml_parser->expat_mem.realloc_fcn = xml_ctxt->memory_handler.f_realloc ;
    new_xml_parser->expat_mem.free_fcn = xml_ctxt->memory_handler.f_free ;
  }

  new_xml_parser->ctxt = XML_ParserCreate_MM(NULL, &new_xml_parser->expat_mem, &sep);
  if (new_xml_parser->ctxt == NULL) {
    xmlg_parser_free(new_xml_parser, new_xml_parser);
    return FALSE;
  }

  /* Turn prefix mapping on. */
  XML_SetReturnNSTriplet(new_xml_parser->ctxt, 1);

  XML_SetUserData(new_xml_parser->ctxt, (void *)(new_xml_parser)) ;
  XML_SetElementHandler(new_xml_parser->ctxt, expat_start_element_ns_cb,
                        expat_end_element_ns_cb) ;
  XML_SetStartNamespaceDeclHandler(new_xml_parser->ctxt, expat_start_namespace_cb) ;
  XML_SetCharacterDataHandler(new_xml_parser->ctxt, expat_characters_data_cb) ;
  XML_SetXmlDeclHandler(new_xml_parser->ctxt, expat_xml_decl_cb) ;

  XML_SetStartDoctypeDeclHandler(new_xml_parser->ctxt, expat_xml_dtd_cb) ;

  if (filter_chain != NULL) {
    xmlg_p_set_parse_error_cb(new_xml_parser, filter_chain->parse_error_cb) ;
  }

  *xml_parser = new_xml_parser;

  return TRUE;
}

void xmlg_p_destroy(
      xmlGParser **xml_parser,
      HqBool error_occurred)
{
  xmlGParserCommon *c ;
  xmlGFilterChain *filter_chain ;

  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  XMLGASSERT(*xml_parser != NULL, "xml_parser pointer is NULL");

  c = xmlg_get_parser_common(*xml_parser);
  filter_chain = c->filter_chain ;

  if (error_occurred) {
    if (c->filter_chain != NULL)
      xmlg_fc_execute_undo(filter_chain) ;
  }

  /* This destroys everything except the memory xml_parser function
     pointers. */
  xmlg_parser_common_terminate(*xml_parser);

  XML_ParserFree((*xml_parser)->ctxt);

  /* It is safe to do this. */
  xmlg_parser_free(*xml_parser, *xml_parser);
  (*xml_parser) = NULL;
}

/* ============================================================================
 * XML parse interfaces.
 */
HqBool xmlg_p_parse_byte_stream_zero_copy(
      xmlGParser *xml_parser,
      void *inputStream,
      xmlGByteStreamReadCallbackZeroCopy fRead)
{
  uint8 *buf ;
  int res ;
  int parseResult ;
  xmlGParserCommon *c ;
  xmlGFilterChain *filter_chain ;

  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  XMLGASSERT(inputStream != NULL, "inputstream is NULL");
  XMLGASSERT(fRead != NULL, "fRead is NULL");

  c = xmlg_get_parser_common(xml_parser);
  filter_chain = c->filter_chain ;

  for (;;) {
    if ((res = fRead(inputStream, &buf)) < 0) {
      xmlg_parser_error_abort(xml_parser, TRUE,
                              NAME_AND_LENGTH("Unable to read XML byte stream")) ;
      break ;
    }

    /* This ensures that all bytes are consumed. */
    parseResult = XML_Parse(xml_parser->ctxt, (const char *)buf, res, res == 0) ;

    /* Pick up XML parse errors */
    if (parseResult != XML_STATUS_OK) {
      int errstr_len = 0 ;
      const char *errstr ;
      errstr = XML_ErrorString(XML_GetErrorCode(xml_parser->ctxt)) ;
      if (errstr != NULL)
        errstr_len = strlen_int32(errstr) ;
      xmlg_parser_error_abort(xml_parser, TRUE, (uint8 *)errstr, errstr_len) ;
      break ;
    }

    if (res == 0)
      break ;

    /* Pick up thrown errors and parse errors immediately */
    if (c->error_abort || c->success_abort)
      break ;
  }

  if (c->error_abort) {
    if (c->filter_chain != NULL)
      xmlg_fc_execute_undo(filter_chain) ;
    return FALSE;
  }

  return TRUE;
}

HqBool xmlg_p_parse_byte_stream(
      xmlGParser *xml_parser,
      void *inputStream,
      xmlGByteStreamReadCallback fRead)
{
  unsigned char *buf ;
  int size = 1024 ;
  int res ;
  int parseResult ;
  xmlGParserCommon *c ;
  xmlGFilterChain *filter_chain ;

  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  XMLGASSERT(inputStream != NULL, "inputstream is NULL");
  XMLGASSERT(fRead != NULL, "fRead is NULL");

  c = xmlg_get_parser_common(xml_parser);
  filter_chain = c->filter_chain ;

  for (;;) {
    if ((buf = (unsigned char *)XML_GetBuffer(xml_parser->ctxt, size)) == NULL) {
      xmlg_parser_error_abort(xml_parser, TRUE,
                              NAME_AND_LENGTH("Unable to allocate XML parse buffer")) ;
      break ;
    }

    if ((res = fRead(inputStream, buf, size)) < 0) {
      xmlg_parser_error_abort(xml_parser, TRUE,
                              NAME_AND_LENGTH("Unable to read XML byte stream")) ;
      break ;
    }

    parseResult = XML_ParseBuffer(xml_parser->ctxt, res, res == 0);

    /* Pick up XML parse errors */
    if (parseResult != XML_STATUS_OK) {
      int errstr_len = 0 ;
      const char *errstr ;
      errstr = XML_ErrorString(XML_GetErrorCode(xml_parser->ctxt)) ;
      if (errstr != NULL)
        errstr_len = strlen_int32(errstr) ;
      xmlg_parser_error_abort(xml_parser, TRUE, (uint8 *)errstr, errstr_len) ;
      break ;
    }

    if (res == 0)
      break ;

    /* Pick up thrown errors and parse errors immediately */
    if (c->error_abort || c->success_abort)
      break ;
  }

  if (c->error_abort) {
    if (c->filter_chain != NULL)
      xmlg_fc_execute_undo(filter_chain) ;
    return FALSE;
  }

  return TRUE;
}

HqBool xmlg_p_parse_chunk(
      xmlGParser *xml_parser,
      uint8 *buf,
      uint32 buflen,
      HqBool terminate)
{
  int parseResult ;
  xmlGParserCommon *c ;
  xmlGFilterChain *filter_chain ;

  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL") ;
  XMLGASSERT(! terminate ? buf != NULL : TRUE, "buf is NULL") ;
  XMLGASSERT( buf == NULL ? buflen == 0 : TRUE, "buf is NULL") ;

  c = xmlg_get_parser_common(xml_parser) ;
  filter_chain = c->filter_chain ;

  parseResult = XML_Parse(xml_parser->ctxt,
                          (const char *)buf,
                          (int)buflen,
                          (int)terminate) ;

  /* Pick up XML parse errors */
  if (parseResult != XML_STATUS_OK) {
    int errstr_len = 0 ;
    const char *errstr ;
    errstr = XML_ErrorString(XML_GetErrorCode(xml_parser->ctxt)) ;
    if (errstr != NULL)
      errstr_len = strlen_int32(errstr) ;
    xmlg_parser_error_abort(xml_parser, TRUE, (uint8 *)errstr, errstr_len) ;
  }

  /* Pick up thrown errors and parse errors immediately */
  if (c->error_abort) {
    if (c->filter_chain != NULL)
      xmlg_fc_execute_undo(filter_chain) ;

    return FALSE;
  }

  /* Aborting a parse does not cause an error */
  if (c->success_abort)
    return TRUE;

  return TRUE;
}

/* ============================================================================
 * Abort and information functions.
 */
void xmlg_p_abort_parse(
      xmlGParser *xml_parser)
{
  xmlGParserCommon *c;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  c = xmlg_get_parser_common(xml_parser);
  XMLGASSERT(c != NULL, "common is NULL");

  c->success_abort = TRUE;
  /* setting the callback functions to NULL ensures we stop */
  XML_SetElementHandler(xml_parser->ctxt, NULL, NULL);
  XML_SetStartNamespaceDeclHandler(xml_parser->ctxt, NULL);
  XML_SetCharacterDataHandler(xml_parser->ctxt, NULL);
  XML_SetXmlDeclHandler(xml_parser->ctxt, NULL) ;
  XML_SetStartDoctypeDeclHandler(xml_parser->ctxt, NULL) ;
}

/* Note that obtaining the line and column depth is an expensive operation.
 * Hence, we do not keep the handler line and column in sync unless someone
 * asks for it - which typically is when debugging or an error condition
 * occurs as we want positional information.
 */
HqBool xmlg_parser_line_and_column(
      xmlGParser *xml_parser,
      uint32 *line,
      uint32 *column)
{
  xmlGParserCommon *c;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  c = xmlg_get_parser_common(xml_parser);
  XMLGASSERT(c != NULL, "common is NULL");
  XMLGASSERT(line != NULL, "line is NULL");
  XMLGASSERT(column != NULL, "column is NULL");

  if (! c->success_abort && ! c->error_abort) {
    *line = (uint32)XML_GetCurrentLineNumber(xml_parser->ctxt);
    *column = (uint32)XML_GetCurrentColumnNumber(xml_parser->ctxt);
    c->prev_line = *line;
    c->prev_column = *column;
  } else {
    *line = c->prev_line;
    *column = c->prev_column;
  }
  return TRUE;
}

/* ============================================================================
* Log stripped */
