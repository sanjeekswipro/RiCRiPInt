#ifndef __XMLGTYPE_H__
#define __XMLGTYPE_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!export:xmlgtype.h(EBDSDK_P.1) $
 * $Id: export:xmlgtype.h,v 1.30.10.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Generic XML parser types.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XMLG_SW_COMPAT
#include "hqtypes.h"
#endif

#include <stdarg.h>
#include "hqunicode.h"
#include "hqnuri.h"

enum {
 XMLG_RESULT_ERROR = FALSE,  /* error - value MUST be FALSE */
 XMLG_RESULT_FORWARD = TRUE, /* pass on to next filter - value MUST be TRUE */
 XMLG_RESULT_HANDLED         /* do not pass on to next filter */
} ;

/**
 * \brief Memory management structure used for plugging various XML
 * memory management layers.
 */
typedef struct xmlGMemoryHandler {
  /*@only@*/ /*@null@*/
  void * (*f_malloc)(
        size_t size) ;

  /*@only@*/ /*@null@*/
  void * (*f_realloc)(
        /*@only@*/ /*@out@*/ /*@null@*/
        void *memPtr,
        size_t size) ;

  void (*f_free)(
        /*@only@*/ /*@out@*/ /*@null@*/
        void *memPtr) ;
} xmlGMemoryHandler ;

/**
 * \brief An XML sub-system context instance.
 */
typedef struct xmlGContext xmlGContext ;

/**
 * \brief An XML parse context instance.
 */
typedef struct xmlGParser xmlGParser ;

/**
 * \brief An XML filter context instance.
 */
typedef struct xmlGFilter xmlGFilter ;

/**
 * \brief An XML filter chain context instance.
 */
typedef struct xmlGFilterChain xmlGFilterChain ;

/**
 * \brief XML callback function table.
 */
typedef struct xmlGFunctTable xmlGFunctTable ;

/**
 * \brief XML start element callback function attributes.
 */
typedef struct xmlGAttributes xmlGAttributes ;


/**
 * \brief Interned string.
 *
 * This is an opaque pointer for libgenxml and is implemented by the client
 * code.
 */
typedef struct xmlGIStr xmlGIStr ;

/**
 * \brief A constructed valid child table.
 *
 * Used for promoting parent valid child blocks by child elements.
 */
typedef struct xmlGValidChildTable xmlGValidChildTable ;

/**
 * \brief Callback to read more data from XML data stream.
 *
 * \param input_stream User defined input stream pointer. This is the
 * pointer which is passed into \ref xmlg_p_parse_byte_stream.
 *
 * \param buf The buffer where data ought to be written. The maximum
 * number of bytes which can be written to this buffer is \p buflen.
 *
 * \param buflen Maximum number of bytes to place in \p buf.
 *
 * This callback gets called to provide more data to the XML parser
 * when parsing via the \ref xmlg_p_parse_byte_stream interface.
 *
 * \retval 0 when there is no more data.
 * \retval >0 number off bytes placed in \a buf
 * \retval <0 of an error occured
 */
typedef
int32 (*xmlGByteStreamReadCallback)(
      /*@in@*/ /*@notnull@*/
      void *input_stream,
      /*@in@*/ /*@notnull@*/
      uint8 *buf,
      uint32 buflen) ;


/**
 * \brief Callback to read more data from XML data stream.
 *
 * \param input_stream User defined input stream pointer. This is the
 * pointer which is passed into \ref xmlg_p_parse_byte_stream_zero_copy.
 *
 * \param buf The buffer which will be consumed. Will set the buffer
 * to NULL if no more data or an error occurs.
 *
 * This callback gets called to provide more data to the XML parser
 * when parsing via the \ref xmlg_p_parse_byte_stream_zero_copy
 * interface.
 *
 * \retval 0 when there is no more data.
 * \retval >0 number off bytes available in \a buf
 * \retval <0 of an error occured
 */
typedef
int32 (*xmlGByteStreamReadCallbackZeroCopy)(
      /*@in@*/ /*@notnull@*/
      void *input_stream,
      /*@in@*/ /*@notnull@*/
      uint8 **buf) ;

/**
 * \brief MapUri callback.
 *
 * \param from_uri The URI to map from.
 *
 * \param to_uri The URI which will replace from_uri.
 *
 * \retval TRUE Success.
 *
 * \retval FALSE Failure. Parsing will stop immediately and FALSE will
 * be returned from the parse function (Eg: \ref
 * xmlg_p_parse_byte_stream).
 *
 * Its important that to_uri is always set to some value, even if its
 * the from_uri.
 */
typedef
HqBool (*xmlGMapUriCallback)(
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *from_uri,
      /*@in@*/ /*@null@*/
      const xmlGIStr **to_uri) ;

/**
 * \brief Start element callback.
 *
 * \param filter The parse filter.
 *
 * \param localname The element name.
 *
 * \param prefix The element's prefix.
 *
 * \param uri The element's name space.
 *
 * \param attributes The element's attributes.
 *
 * \retval TRUE Success. Parsing will continue and the next filter in
 * the chain will be called.
 *
 * \retval FALSE Failure. Parsing will stop immediately and FALSE will
 * be returned from the parse function (Eg: \ref
 * xmlg_p_parse_byte_stream). End element callbacks for successful start
 * element callbacks will be called with success equal to FALSE.
 *
 * \retval XMLG_RESULT_HANDLED Success, but the call will not be passed
 * down the filter chain.
 */
typedef
int32 (*xmlGStartElementCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@notnull@*/
      xmlGAttributes *attributes) ;

/**
 * \brief End element callback.
 *
 * \param filter The parse filter.
 *
 * \param localname The element name.
 *
 * \param prefix The element's prefix.
 *
 * \param uri The element's name space.
 *
 * \param success This is always set to TRUE during normal XML processing.
 * If this is set to FALSE, it means that an error has occured, the error
 * stack was enabled and all the end callbacks are being called. This is useful
 * for backing out state changes which start element callbacks may have done.
 *
 * \retval TRUE Success. Parsing will continue and the next filter in
 * the chain will be called.
 *
 * \retval FALSE Failure. Parsing will stop immediately and FALSE will
 * be returned from the parse function (Eg: \ref
 * xmlg_p_parse_byte_stream). End element callbacks for successful start
 * element callbacks will be called with success equal to FALSE.
 *
 * \retval XMLG_RESULT_HANDLED Success, but the call will not be passed
 * down the filter chain.
 */
typedef
int32 (*xmlGEndElementCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      HqBool success) ;

/**
 * \brief Character data callback.
 *
 * \param filter The parse filter.
 *
 * \param buf The character data.
 *
 * \param buflen Number of bytes in \p buf.
 *
 * \retval TRUE Success. Parsing will continue and the next filter in
 * the chain will be called.
 *
 * \retval FALSE Failure. Parsing will stop immediately and FALSE will
 * be returned from the parse function (Eg: \ref
 * xmlg_p_parse_byte_stream). End element callbacks for successful start
 * element callbacks will be called with success equal to FALSE.
 *
 * \retval XMLG_RESULT_HANDLED Success, but the call will not be passed
 * down the filter chain.
 */
typedef
int32 (*xmlGCharactersCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@null@*/
      const uint8 *buf,
      uint32 buflen) ;

/**
 * \brief XML declaration callback.
 *
 * \param filter The parse filter.
 *
 * \param version The version value.
 *
 * \param version_len The version length.
 *
 * \param encoding The encoding value.
 *
 * \param encoding_len The encoding length.
 *
 * \param standalone The standalone parameter will be -1, 0, or 1
 * indicating respectively that there was no standalone parameter in
 * the declaration, that it was given as no, or that it was given as
 * yes.
 *
 * \retval TRUE Success. Parsing will continue and the next filter in
 * the chain will be called.
 *
 * \retval FALSE Failure. Parsing will stop immediately and FALSE will
 * be returned from the parse function (Eg: \ref
 * xmlg_p_parse_byte_stream). End element callbacks for successful start
 * element callbacks will be called with success equal to FALSE.
 *
 * \retval XMLG_RESULT_HANDLED Success, but the call will not be passed
 * down the filter chain.
 */
typedef
int32 (*xmlGXmlDeclarationCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      const uint8 *version,
      uint32 version_len,
      /*@in@*/ /*@null@*/
      const uint8 *encoding,
      uint32 encoding_len,
      int32 standalone) ;

/**
 * \brief XML document type definition start callback.
 *
 * \param filter The parse filter.
 *
 * \param doctypeName The document type definition name.
 *
 * \param doctypeName_len The document type definition name length.
 *
 * \param sysid The system identifier if present.
 *
 * \param sysid_len The system identifier length.
 *
 * \param pubid The public identifier if present.
 *
 * \param pubid_len The public identifier length.
 *
 * \param has_internal_subset Does the document type definition include an
 * internal subset.
 *
 * \retval TRUE Success. Parsing will continue and the next filter in
 * the chain will be called.
 *
 * \retval FALSE Failure. Parsing will stop immediately and FALSE will
 * be returned from the parse function (Eg: \ref
 * xmlg_p_parse_byte_stream). End element callbacks for successful start
 * element callbacks will be called with success equal to FALSE.
 *
 * \retval XMLG_RESULT_HANDLED Success, but the call will not be passed
 * down the filter chain.
 */
typedef
int32 (*xmlGXmlDTDStartCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
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

/**
 * \brief Namespace callback.
 *
 * \param filter The parse filter.
 *
 * \param prefix The namespace prefix.
 *
 * \param uri The namespace URI.
 *
 * \retval TRUE Success. Parsing will continue and the next filter in
 * the chain will be called.
 *
 * \retval FALSE Failure. Parsing will stop immediately and FALSE will
 * be returned from the parse function (Eg: \ref
 * xmlg_p_parse_byte_stream). End element callbacks for successful start
 * element callbacks will be called with success equal to FALSE.
 *
 * \retval XMLG_RESULT_HANDLED Success, but the call will not be passed
 * down the filter chain.
 */
typedef
int32 (*xmlGNamespaceCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri) ;

/**
 * \brief Type converter callback.
 */
typedef HqBool (*xmlGTypeConverter)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xmlGIStr *attrlocalname,
      /*@in@*/ /*@notnull@*/
       utf8_buffer* value,
      /*@notnull@*/ void *data) ;

/**
 * \brief XML error callback.
 *
 * Gets called if an internal libgenxml error occurs, an XML syntax
 * error occurs or if any parse error occur.
 *
 * \param[in] xml_parser The parse handler.
 *
 * \param[in] detail A descriptive string.
 *
 * \param[in] detail_len Length of descriptive string.
 */
typedef
void (*xmlGParseErrorCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *uri,
      uint32 line,
      uint32 column,
      /*@in@*/ /*@null@*/
      uint8 *detail,
      int32  detail_len) ;

/**
 * \brief User XML error callback.
 *
 * Gets called if a user defined XML processing callback (element
 * start/end/characters etc..) returns FALSE.
 *
 * \param[in] handler The parse handler.
 */
typedef
void (*xmlGUserErrorCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      uint32 line,
      uint32 column) ;

/**
 * \brief Validity error callback.
 *
 * Gets called if child validity checking is in place and an XML child validity
 * error is detected. The element detail is the child.
 *
 * \param filter The parse filter.
 * \param format The format of the message in a snprintf like syntax.
 * \param ... The format arguments.
 *
 * \retval TRUE Success. Parsing will continue.
 * \retval FALSE Failure. Parsing will stop immediately and FALSE will
 * be returned from the parse function (Eg: \ref xmlg_p_parse_byte_stream).
 */
typedef
HqBool (*xmlGValidityErrorCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@notnull@*/
      xmlGAttributes *attrs,
      int32 error_type,
      /*@in@*/ /*@notnull@*/
      char *format,
      ...) ;

typedef
void (*xmlGFilterCleanupCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif
