/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlparse.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements the various parse interfaces we expose to the core.
 *
 * Also contains the namespace detection mechanism which registers XML
 * callbacks.
 */

#include "core.h"
#include "mmcompat.h"          /* mm_alloc_with_header etc.. */
#include "swerrors.h"          /* error_handler */
#include "hqmemcpy.h"          /* HqMemCpy */
#include "gcscan.h"            /* ps_scan_file */
#include "fileio.h"            /* FILELIST */
#include "monitor.h"           /* monitorf */
#include "xmlg.h"              /* xmlG interfaces */
#include "xmlcontext.h"
#include "xmldebug.h"
#include "timing.h"

#include "namedef_.h"

#define MAX_URI_LENGTH 65535u

/* ====================================
 * Read XML stream
 */

#if defined(XML_COPYBUF_INTERFACE)
static int32 xml_stream_read(
  void *inputStream,
  uint8 *buffer,
  uint32 buflen)
{
  int32 bytes ;
  FILELIST *flptr ;

  HQASSERT((inputStream != NULL), "NULL input stream") ;
  HQASSERT((buffer != NULL), "NULL buffer pointer") ;
  HQASSERT((buflen > 0), "zero length buffer") ;

  /* Cast to RIP types */
  flptr = (FILELIST *)inputStream ;

  /* Keep reading until buffer full or EOF - EOF will be flagged on next call */
  remaining = CAST_UNSIGNED_TO_INT32(buflen) ;
  if ( file_read(flptr, buffer, CAST_UNSIGNED_TO_INT32(buflen), &bytes) == 0 )
    return -1 ;

  /* Return number of bytes read */
  return bytes ;
}
#else
static int32 xml_stream_read_zero_copy(
  void *inputStream,
  uint8 **buffer)
{
  int32 bytes ;
  FILELIST *flptr ;

  /* A reasonable amount of data ought to be read in a single
     chunk. */
#define ZERO_COPY_READ_BUFFER_SIZE 4096

  HQASSERT((inputStream != NULL), "NULL input stream") ;
  HQASSERT((buffer != NULL), "NULL buffer pointer") ;

  /* Cast to RIP types */
  flptr = (FILELIST *)inputStream ;

  /* Keep reading until we get some bytes or EOF */
  if (! GetFileBuff(flptr, ZERO_COPY_READ_BUFFER_SIZE, buffer, &bytes) ) {
    if ( isIIOError(flptr) ) {
      return(-1) ;
    }
  }

  /* Return number of bytes actually read. */
  return(bytes) ;
}
#endif

/* ====================================
 * XML parse entry points for the core
 */

/* The maximum number of XML parse instances we allow. */
#define MAX_ACTIVE_XML_PARSE_INSTANCES 100

static uint32 xml_active_parse_count = 0 ;

struct xml_chunk_parser_t {
  FILELIST *flptr ;
  xmlGParser *xml_parser ;
  hqn_uri_t *stream_uri ;
#if defined(DEBUG_BUILD)
  xmlGFilter *debug_filter ;
#endif
} ;

/**
 * Prepare to start consuming an XML stream in a piecemeal fashion; no xml is
 * consumed at this point and no callbacks will be made. xml_parse_chunk() must
 * be called repeatedly to consume the XML.
 */
Bool xml_parse_chunk_init(
      FILELIST *flptr,
      xmlGFilterChain *filter_chain,
      xml_chunk_parser_t **chunk_parser)
{
  xml_chunk_parser_t *new_chunk_parser ;
  XMLExecContext *p_xmlexec_context ;
  xmlGParser *xml_parser ;
#if defined(DEBUG_BUILD)
  xmlGFilter *debug_filter = NULL ;
#endif

  HQASSERT((flptr != NULL),"NULL flptr") ;
  HQASSERT((chunk_parser != NULL),"NULL chunk_parser") ;
  p_xmlexec_context = SLL_GET_HEAD(&xml_context.sls_contexts, XMLExecContext, sll) ;
  HQASSERT((p_xmlexec_context != NULL), "NULL xmlexec pointer") ;

  *chunk_parser = NULL ;

  if ((new_chunk_parser = xml_subsystem_malloc(sizeof(xml_chunk_parser_t))) == NULL) {
    return error_handler(VMERROR) ;
  }

  /* Create XML parser with context set correctly */
  if (! xmlg_p_new(core_xml_subsystem, &xml_parser, &xmlexec_memory_handlers,
                   filter_chain)) {
    xml_subsystem_free(new_chunk_parser) ;
    return error_handler(UNDEFINED) ;
  }

  if ( p_xmlexec_context->map_uri != NULL )
    xmlg_p_set_map_uri_cb(xml_parser, p_xmlexec_context->map_uri) ;

  xml_active_parse_count++ ;

  if (xml_active_parse_count > MAX_ACTIVE_XML_PARSE_INSTANCES) {
    xmlg_p_destroy(&xml_parser, FALSE) ;
    xml_subsystem_free(new_chunk_parser) ;
    return detail_error_handler(UNDEFINED,
             "Maximum number of XML parse instances exceeded.") ;
  }

#if defined(DEBUG_BUILD)
  if ( (debug_xml & DEBUG_XML_PARSE) != 0 ) {
    hqn_uri_t *base_uri ;
    hqn_uri_t *stream_uri ;
    uint8 *base, *uri ;
    uint32 base_len, uri_len ;

    if (! psdev_uri_from_open_file(flptr, &stream_uri)) {
      xmlg_p_destroy(&xml_parser, FALSE) ;
      xml_subsystem_free(new_chunk_parser) ;
      return error_handler(UNDEFINED) ;
    }
    base_uri = xmlg_fc_get_base_uri(filter_chain) ;

    if (hqn_uri_get_field(stream_uri,
                          &uri, &uri_len, HQN_URI_ENTIRE) &&
        hqn_uri_get_field(base_uri,
                          &base, &base_len, HQN_URI_ENTIRE)) {
      monitorf((uint8*)"_____________ start parse(%03d): %.*s; base=%.*s\n",
        xml_active_parse_count, uri_len, uri, base_len, base) ;
    } else {
      HQFAIL("Unable to obtain uri or base names.") ;
      xmlg_p_destroy(&xml_parser, FALSE) ;
      xml_subsystem_free(new_chunk_parser) ;
      return error_handler(TYPECHECK);
    }
  }

  if ( (debug_xml & (DEBUG_XML_ATTRIBUTES|
                     DEBUG_XML_STARTEND|
                     DEBUG_XML_CHARACTERS)) != 0 ) {

    /* install the debug filter */
    if (! debug_xml_filter_init(filter_chain, 0, &debug_filter)) {
      xmlg_p_destroy(&xml_parser, FALSE) ;
      xml_subsystem_free(new_chunk_parser) ;
      return detail_error_handler(UNDEFINED,
               "Unable to install XML debug filter.") ;
    }

  }
#endif

  new_chunk_parser->flptr = flptr ;
  new_chunk_parser->xml_parser = xml_parser ;
  new_chunk_parser->stream_uri = xmlg_fc_get_uri(filter_chain) ;
#if defined(DEBUG_BUILD)
  new_chunk_parser->debug_filter = debug_filter ;
#endif

  *chunk_parser = new_chunk_parser ;
  return TRUE ;
}

Bool xml_parse_chunk(
      xml_chunk_parser_t *chunk_parser,
      Bool *more_data)
{
  HqBool status = FALSE ;
  FILELIST *flptr ;
  xmlGParser *xml_parser ;
  uint8 *buf = NULL;
  int32 res ;
  hqn_uri_t *stream_uri ;

  HQASSERT((chunk_parser != NULL),"NULL chunk_parser") ;

  xml_parser = chunk_parser->xml_parser ;
  flptr = chunk_parser->flptr ;
  stream_uri = chunk_parser->stream_uri ;

  HQASSERT((xml_parser != NULL),"NULL xml_parser") ;
  HQASSERT((flptr != NULL),"NULL flptr") ;
  HQASSERT((stream_uri != NULL),"NULL stream_uri") ;

  /* Read a chunk of bytes from the file stream. */
  res = xml_stream_read_zero_copy(flptr, &buf) ;
  if (res < 0) {
    uint8 *uri_name ;
    uint32 uri_name_len ;

    *more_data = FALSE ;

    if (! hqn_uri_get_field(stream_uri, &uri_name, &uri_name_len, HQN_URI_PATH))
      return detail_error_handler(IOERROR, "Error while trying to read data from an XML stream.") ;

    return detailf_error_handler(IOERROR, "Error while trying to read data from %.*s.", uri_name_len, uri_name) ;
  }

  *more_data = (res != 0) ;
  status = xmlg_p_parse_chunk(xml_parser, buf, res, res == 0) ;

  return status ;
}

void xml_parse_chunk_finish(
      xml_chunk_parser_t **chunk_parser,
      Bool error_occurred)
{
  xmlGParser *xml_parser ;
  hqn_uri_t *stream_uri ;
#if defined(DEBUG_BUILD)
  xmlGFilter *debug_filter ;
#endif

  HQASSERT((chunk_parser != NULL),"NULL chunk_parser") ;
  HQASSERT((*chunk_parser != NULL),"NULL *chunk_parser") ;

  xml_parser = (*chunk_parser)->xml_parser ;
  stream_uri = (*chunk_parser)->stream_uri ;

  HQASSERT((xml_parser != NULL),"NULL xml_parser") ;
  HQASSERT((stream_uri != NULL),"NULL stream_uri") ;

#if defined(DEBUG_BUILD)
  debug_filter = (*chunk_parser)->debug_filter ;

  /*
   Destruction of the filter chain will end up calling the debug
   filter cleanup.

  if ( (debug_xml & (DEBUG_XML_ATTRIBUTES|
                     DEBUG_XML_STARTEND|
                     DEBUG_XML_CHARACTERS)) != 0 ) {
    debug_xml_filter_dispose(&debug_filter) ;
  }
  */

  if ((debug_xml & DEBUG_XML_PARSE) != 0) {
    uint8 *uri ;
    uint32 uri_len ;

    if (hqn_uri_get_field(stream_uri,
                          &uri, &uri_len, HQN_URI_ENTIRE)) {
      monitorf((uint8*)"_____________ end parse(%03d): %.*s\n",
        xml_active_parse_count, uri_len, uri) ;
    } else {
      HQFAIL("Unable to obtain uri.") ;
      /* May as well fall through and de-allocate the memory. */
    }
  }
#endif

  xmlg_p_destroy(&xml_parser, error_occurred) ;
  xml_subsystem_free(*chunk_parser) ;
  *chunk_parser = NULL ;

  xml_active_parse_count-- ;
}

Bool xml_parse_stream(
      FILELIST *flptr,
      xmlGFilterChain *filter_chain)
{
  HqBool status ;
  xml_chunk_parser_t *chunk_parser ;

  HQASSERT((flptr != NULL),"NULL flptr") ;

  probe_begin(SW_TRACE_INTERPRET_XML, 0) ;
  status = xml_parse_chunk_init(flptr, filter_chain, &chunk_parser) ;
  if ( status ) {
    xmlGParser *xml_parser = chunk_parser->xml_parser ;
    HQASSERT((xml_parser != NULL),"NULL xml_parser") ;

    /* Parse the entire byte stream. */
#if defined(XML_COPYBUF_INTERFACE)
    status = xmlg_p_parse_byte_stream(xml_parser, flptr, xml_stream_read) ;
#else
    status = xmlg_p_parse_byte_stream_zero_copy(xml_parser, flptr,
                                                xml_stream_read_zero_copy) ;
#endif

    /* We do not need to flag an error condition here as the XML
       callbacks when parsing the entire stream will have done that for
       us. */
    xml_parse_chunk_finish(&chunk_parser, FALSE) ;
  }
  probe_end(SW_TRACE_INTERPRET_XML, 0) ;

  return status ;
}

void xml_streams_init(void)
{
  /* Nothing needs to be done yet. */
}

void xml_streams_finish(void)
{
  /* Nothing needs to be done yet. */
}

/* Parses the XML content from the specified URI. */
Bool xml_parse_uri_stream(
      hqn_uri_t *uri,
      xmlGFilterChain *filter_chain,
      Bool optional,
      FILELIST **flptr)
{
  OBJECT ofile = OBJECT_NOTVM_NOTHING ;
  Bool status ;
  Bool exists;
  STAT stat;

  HQASSERT(uri != NULL, "uri is NULL") ;
  HQASSERT(flptr != NULL, "flptr is NULL") ;
  HQASSERT(! error_signalled(), "Shouldn't be opening URI in error condition") ;

  if ( optional ) {
    /* Its ok for the URI to not exist */
    if ( !stat_from_psdev_uri(uri, &exists, &stat) ) {
      return(FALSE);
    }
    if ( !exists ) {
      return(TRUE);
    }
  }

  if (! open_file_from_psdev_uri(uri, &ofile, FALSE))
    return FALSE ;

  *flptr = oFile(ofile) ;

  /* If an error occured in the parse of this sub-XML file, the error
   * would have been reported already - so simply start recurring out
   * of the XML parsers.
   */
  status = xml_parse_stream(oFile(ofile), filter_chain) ;

  *flptr = NULL ;

  (void)xml_file_close(&ofile);

  return status;
}


/* ============================================================================
* Log stripped */
