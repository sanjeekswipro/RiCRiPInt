#ifndef __XMLG_H__
#define __XMLG_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!export:xmlg.h(EBDSDK_P.1) $
 * $Id: export:xmlg.h,v 1.49.4.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \ingroup libgenxml
 * \brief Public interface for a generic XML SAX2 like parser.
 *
 * Implements generic XML processing interfaces above MSXML, libxml,
 * Xerces-c and expat. The intention is that either of these real XML
 * parsers can be used behind the scenes - depending on the
 * requirements of the product being built. The MSXML, libxml and
 * Xerces-c back ends have been removed as they were not being
 * maintained appropriately. Having said that, the old code will aid
 * anyone trying to reconstruct one of these back end parsers. See
 * libgenxml makefile.jam for details.
 *
 * SAX2 is an event-driven API in which the contents of an XML
 * document are accessed through callback subroutines that fire based
 * on various XML parsing events (the beginning of an element, the end
 * of an element, character data, etc.) For the purpose of this API, a
 * SAX2 driver (sometimes called a SAX2 generator) can be understood
 * to mean any C routine which can generate SAX2 events.
 *
 * In the most common case, a SAX2 driver acts as a proxy between an
 * XML parser and the one or more handler classes written by the
 * developer. The handler methods detailed in the SAX2 API are called
 * as the parser makes its way through the document, thereby providing
 * access to the contents of that XML document. In fact, this is
 * precisely what SAX2 was designed for: to provide a simple means to
 * access information stored in XML.
 *
 * In this API, the functions which work on the xmlGFilter type
 * provide a SAX2 driver which uses the expat XML parser to read
 * through an XML byte stream and generate XML events which are
 * forwarded through a XML filters.
 *
 * An XML filter is simply a class that is passed as the event handler
 * to another class that generates SAX2 events (xmlGFilter),
 * then forwards all or some of those events on the next handler (or
 * filter) in the processing chain. A filter may prune the document
 * tree by not forwarding events for elements with a given name (or
 * that meet some other condition), while in other cases, a filter
 * might generate its own new events to add child elements to certain
 * elements in the existing document stream. Also, element attributes
 * can be added or removed or the character data altered in some
 * way. Really any class that is able to receive SAX2 events, then
 * call event methods on another SAX2 handler in a way that alters the
 * document stream can be seen as a XML filter.
 *
 * In practice, SAX2 filters are like conceptual cousins of many of
 * the standard UNIX tools. By themselves, these tools often perform
 * only a single, simple task, but when piped together they are
 * capable of astonishing feats. In the same way, the real power of
 * XML filters is derived from the fact that simpler, easy-to-maintain
 * filters may be chained together to produce complex XML data
 * transformations.
 *
 * This interface provides XML filter chains to set up function
 * callbacks when XML content is being processed. XML filter chains do
 * not necessarily have an XML parse instance feeding them. Arbitrary
 * XML can be fed into an XML filter chain by simply calling the
 * filter chains feed functions directly. Instead, the XML parsers now
 * allow a single filter chain to be attached to them. When parsing is
 * initiated, the parser will feed its attached filter chain via the
 * filter chains feed functions. This is like an input adapter to the
 * filter chain. For example, one could imagine writing an input
 * adapter which slurped data from a relational database and fed a
 * filter chain. The advantage of this approach is that the filter
 * chain implementation does not change for any particular adapater.
 *
 * A filter chain is made up of filters. Each filter can have its own
 * user defined context. Filter callbacks can feed further XML into
 * the beginning of its own filter chain at any time. In this
 * scenario, the XML is not executed immediately, but after the last
 * filter in the filter chain has been executed.
 *
 * The depth for each filter is quite likely to be different. If a
 * filter near the beginning of the filter chain returns
 * XMLG_RESULT_HANDLED, it is effectively removing XML from view for
 * subsequent filters and hence their depth will differ.
 *
 * When a filter XML processing callback feeds XML, callbacks are
 * started from the beginning of the filter chain.
 *
 * Function naming convention:
 *  - "xmlg_p_.."  short for "xmlg_parser_.."
 *  - "xmlg_fc_.." short for "xmlg_filter_chain_.."
 *  - "xmlg_f_.."  short for "xmlg_filter_.."
 *  - "..._cb"     short for "..._callback"
 */

#include "mps.h"
#include "hqnuri.h"
#include "xmlgtype.h"
#include "xmlgintern.h"
#include "xmlgattrs.h"
#include "xmlgvalid.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \defgroup libgenxml XML parsing and filtering.
    \ingroup xml
    \{ */

/**
 * \brief Initialize a new XML subsystem.
 *
 * Call this function to inilialize a new instance of an XML
 * subsystem. This function MUST be called at least once before any of
 * the XML functions may be used. Multiple XML sub-system contexts can
 * co-exist in a single address space. This is useful when different
 * memory management and intern handlers need to be used for different
 * softare modules.
 *
 * \param xml_ctxt Pointer returned to a newly created
 * XML subsystem context.
 *
 * \param pool Initialised MPS pool. Must survive the life time of
 * xmlg subsystem.
 *
 * \param memory_handler Pointer to a memory handler structure.
 *
 * \param intern_handler Pointer to a intern handler structure.
 *
 * \param uri_context Pointer to callers context to associate with XML context.
 *
 * \returns TRUE on success, FALSE on failure.
 *
 * \note The memory handler functions used here can be overridden per
 * parse instance. In the event that this is the case, some static
 * memory will still be allocated via the memory handlers specified
 * here.
 */
extern
HqBool xmlg_initialize(
   /*@out@*/ /*@notnull@*/
   xmlGContext **xml_ctxt,
   /*@in@*/ /*@notnull@*/
   mps_pool_t pool,
   /*@null@*/
   xmlGMemoryHandler *memory_handler,
   /*@null@*/ /*@in@*/
   xmlGIStrHandler *intern_handler,
   /*@in@*/ /*@notnull@*/
   hqn_uri_context_t *uri_context) ;

/**
 * \brief Terminate the specified XML subsystem.
 */
extern
void xmlg_terminate(
   /*@in@*/ /*@notnull@*/
   xmlGContext **xml_ctxt) ;

/* ============================================================================
 * XML parse handler interface.
 * ============================================================================
 */

/**
 * \brief Create a new XML parse handler.
 */
extern
HqBool xmlg_p_new(
      /*@in@*/ /*@notnull@*/
      xmlGContext *xml_ctxt,
      /*@out@*/ /*@notnull@*/
      xmlGParser **xml_parser,
      /*@in@*/ /*@null@*/
      xmlGMemoryHandler *memory_handler,
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain) ;

/**
 * \brief Destroy an XML parse handler.
 *
 * \param xml_parser The address of a pointer to a parse handler.
 */
extern
void xmlg_p_destroy(
      /*@only@*/ /*@in@*/ /*@notnull@*/
      xmlGParser **xml_parser,
      HqBool error_occurred) ;

/**
 * \brief Set the global URI mapping function.
 *
 * This callback gets called when-ever a URI is seen and allows one to
 * implement a URI alias mechnism. The returned URI will be used for
 * all subsequent XML callbacks along the filter chain. Function table
 * lookup will use the aliased name, not the original name such that
 * filters will be entirely unaware of the orginal namespaces used in
 * any XML content.
 */
extern
void xmlg_p_set_map_uri_cb(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@null@*/
      xmlGMapUriCallback f) ;

extern
void xmlg_p_set_parse_error_cb(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@null@*/
      xmlGParseErrorCallback f) ;

/**
 * \brief Start parsing a byte stream via a pull interface for more
 * data. The read callback is given a pointer to write the data into
 * which is then always entirely consumed.
 *
 * \retval TRUE  No XML error occured.
 * \retval FALSE An XML or user defined error occured.
 *
 * \note Much more documentation to follow.
 */
extern
HqBool xmlg_p_parse_byte_stream(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@in@*/ /*@notnull@*/
      void *inputStream,
      /*@notnull@*/
      xmlGByteStreamReadCallback f) ;

/**
 * \brief Start parsing a byte stream via a pull interface for more
 * data. The read callback provides a pointer to the data which is
 * always entirely consumed.
 *
 * \retval TRUE  No XML error occured.
 * \retval FALSE An XML or user defined error occured.
 *
 * \note Much more documentation to follow.
 */
HqBool xmlg_p_parse_byte_stream_zero_copy(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@in@*/ /*@notnull@*/
      void *inputStream,
      /*@notnull@*/
      xmlGByteStreamReadCallbackZeroCopy f) ;

/**
 * \brief Start parsing a byte stream via a push interface.
 *
 * \retval TRUE  No error occured.
 * \retval FALSE A error occured.
 *
 * \note Much more documentation to follow.
 */
extern
HqBool xmlg_p_parse_chunk(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@in@*/ /*@notnull@*/
      uint8 *buf,
      uint32 buflen,
      HqBool terminate) ;

/* ============================================================================
 * XML filter chain interface.
 * ============================================================================
 */

/**
 * \brief Create a new XML parse filter chain.
 */
extern
HqBool xmlg_fc_new(
      /*@in@*/ /*@notnull@*/
      xmlGContext *xml_ctxt,
      /*@out@*/ /*@notnull@*/
      xmlGFilterChain **filter_chain,
      /*@in@*/ /*@null@*/
      xmlGMemoryHandler *memory_handler,
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *uri,
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *base_uri,
      /*@in@*/ /*@null@*/
      void *user_data) ;

/**
 * \brief Destroy an XML parse filter chain.
 */
extern
void xmlg_fc_destroy(
      /*@only@*/ /*@in@*/ /*@notnull@*/
      xmlGFilterChain **filter_chain) ;

extern
HqBool xmlg_fc_new_filter(
      xmlGFilterChain *filter_chain,
      xmlGFilter **filter,
      uint32 position,
      void *user_data,
      xmlGFilterCleanupCallback cleanup_cb) ;

extern
HqBool xmlg_fc_delete_filter_position(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      uint32 removal_position) ;

/**
 * \brief Execute an XML start element on the specified filter chain.
 *
 * If this function is called from a filter callback, the start
 * element will NOT get executed until after the last filter in the
 * chain has been executed or a subsequent filter returns
 * XMLG_RESULT_HANDLED.
 *
 * If this function is not called from a filter callback, the start
 * element will be executed immediately.
 */
extern
HqBool xmlg_fc_execute_start_element(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@null@*/
      xmlGAttributes *attributes) ;

/**
 * \brief Execute an XML end element on the specified filter chain.
 *
 * If this function is called from a filter callback, the end
 * element will NOT get executed until after the last filter in the
 * chain has been executed or a subsequent filter returns
 * XMLG_RESULT_HANDLED.
 *
 * If this function is not called from a filter callback, the start
 * element will be executed immediately.
 */
extern
HqBool xmlg_fc_execute_end_element(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      HqBool success) ;

extern
HqBool xmlg_fc_execute_namespace(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri) ;

extern
HqBool xmlg_fc_execute_characters(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@notnull@*/
      const uint8 *buf,
      uint32 buflen) ;

extern
HqBool xmlg_fc_execute_xml_decl(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@notnull@*/
      const uint8 *version,
      uint32 version_len,
      /*@in@*/ /*@null@*/
      const uint8 *encoding,
      uint32 encoding_len,
      int32 standalone) ;

extern
HqBool xmlg_fc_execute_start_dtd(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@notnull@*/
      const uint8 *doctypeName,
      uint32 doctypeName_len,
      /*@in@*/ /*@null@*/
      const uint8 *sysid,
      uint32 sysid_len,
      /*@in@*/ /*@null@*/
      const uint8 *pubid,
      uint32 pubid_len,
      int32 has_internal_subset) ;

extern
int32 xmlg_get_fc_id(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

extern
xmlGFilterChain* xmlg_get_fc(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

extern
void xmlg_fc_set_user_data(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@null@*/
      void *user_data) ;

extern
void xmlg_fc_set_parse_error_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@null@*/
      xmlGParseErrorCallback f) ;

extern
xmlGFilterChain *xmlg_find_filter_chain(
      int32 filter_chain_id) ;

extern /*@null@*/ /*@observer@*/
void* xmlg_fc_get_user_data(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain) ;

extern
hqn_uri_t* xmlg_fc_get_uri(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain) ;

extern
hqn_uri_t* xmlg_fc_get_base_uri(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain) ;

extern
HqBool xmlg_fc_set_base_uri(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *base) ;

/* ============================================================================
 * XML filter interface.
 * ============================================================================
 */

extern /*@null@*/ /*@observer@*/
void* xmlg_get_user_data(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

/**
 * \brief Get the line and column number if available.
 *
 * \returns TRUE if the line and column were available, FALSE if not.
 */
extern
HqBool xmlg_line_and_column(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@out@*/ /*@notnull@*/
      uint32 *line,
      /*@out@*/ /*@notnull@*/
      uint32 *column) ;

extern
uint32 xmlg_get_element_depth(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

extern
hqn_uri_t* xmlg_get_uri(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

extern
hqn_uri_t* xmlg_get_base_uri(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

/**
 * \brief Abort the filter and hence the filter chain and if an XML
 * parser is feeding the chain, abort that too.
 */
extern
void xmlg_abort(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

extern
HqBool xmlg_get_namespace_uri(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      const xmlGIStr *prefix,
      const xmlGIStr **uri) ;

/* Causes a call to the filter cleanup callback, dealloactes the
   filter and removes itself from the filter chain its attached to. */
void xmlg_f_destroy(
      /*@only@*/ /*@in@*/ /*@notnull@*/
      xmlGFilter **filter) ;

/* ============================================================================
 * Filter set/remove callback functions
 * ============================================================================
 */

/**
 * \brief Set the global namespace callback function.
 *
 * This callback gets called when an attribute xmlns="abc" is detected. This
 * callback always gets called before the start element callback for which
 * it resides in. Example:
 *
 * \code
 *   <element xmlns="http://myuri">
 *
 *   </element>
 * \endcode
 *
 * The namespace callback will be called before the "element" start callback
 * is called.
 */
extern
void xmlg_set_namespace_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@null@*/
      xmlGNamespaceCallback f) ;

/**
 * \brief Set the global character callback function.
 *
 * This callback gets called for *all* character data in the XML
 * file. Unlike xmlg_register_characters_cb which only gets called for
 * character data inbetween the start and end of a specified element.
 */
extern
void xmlg_set_character_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@null@*/
      xmlGCharactersCallback f) ;

extern
void xmlg_set_xml_decl_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@null@*/
      xmlGXmlDeclarationCallback f) ;

extern
void xmlg_set_dtd_start_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@null@*/
      xmlGXmlDTDStartCallback f) ;

extern
void xmlg_set_validity_error_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@null@*/
      xmlGValidityErrorCallback f) ;

extern
void xmlg_set_user_error_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@null@*/
      xmlGUserErrorCallback f) ;

extern /*@null@*/
xmlGNamespaceCallback xmlg_remove_namespace_cb(
     /*@in@*/ /*@notnull@*/
     xmlGFilter *filter) ;

extern /*@null@*/
xmlGCharactersCallback xmlg_remove_character_cb(
     /*@in@*/ /*@notnull@*/
     xmlGFilter *filter) ;

extern /*@null@*/
xmlGXmlDeclarationCallback xmlg_remove_xml_decl_cb(
     /*@in@*/ /*@notnull@*/
     xmlGFilter *filter) ;

extern
xmlGXmlDTDStartCallback xmlg_remove_dtd_start_cb(
     /*@in@*/ /*@notnull@*/
     xmlGFilter *filter) ;

/**
 * \brief Register a single start element function given a filter
 * instance.
 *
 * This is useful when parsing and one wants to dynamically manipulate
 * what functions are in the callback table.
 */
extern
HqBool xmlg_register_start_element_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@null@*/
      xmlGStartElementCallback f) ;

/**
 * \brief Register a single end element function given a filter
 * instance.
 */
extern
HqBool xmlg_register_end_element_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@null@*/
      xmlGEndElementCallback f) ;

extern
HqBool xmlg_register_characters_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@null@*/
      xmlGCharactersCallback f) ;

/**
 * \brief Remove a single start element function given a filter
 * instance.
 */
extern /*@null@*/
xmlGStartElementCallback xmlg_deregister_start_element_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri) ;

/**
 * \brief Remove a single start element function given a filter
 * instance.
 */
extern /*@null@*/
xmlGEndElementCallback xmlg_deregister_end_element_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri) ;

/**
 * \brief Remove a single characters element function given a filter
 * instance.
 */
extern /*@null@*/
xmlGCharactersCallback xmlg_deregister_characters_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri) ;

/**
 * \brief Return the current element name and prefix.
 */
extern /*@null@*/
void xmlg_get_current_element(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@out@*/ /*@notnull@*/
      const xmlGIStr **name,
      /*@out@*/ /*@notnull@*/
      const xmlGIStr **prefix) ;

/** \} */

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLG_H__*/
