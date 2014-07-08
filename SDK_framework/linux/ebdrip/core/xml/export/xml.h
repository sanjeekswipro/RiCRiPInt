/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!export:xml.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Entry point for all generic XML functions.
 *
 * Either include individual files directly or pick all of the generic
 * XML headers via this single include.
 */

#ifndef __XML_H__
#define __XML_H__ (1)

#include "mm.h"    /* mm_pool_t */
#include "lists.h" /* sll_link_t */

#include "xmlg.h"
#include "xmlops.h"
#include "xmlintern.h"
#include "xmlstrhash.h"
#include "xmlcache.h"
#include "psdevuri.h"

struct hqn_uri_t ; /* from HQNuri */
struct FILELIST ;  /* from COREfileio */
struct OBJECT ;    /* from COREobjects */

/**
 * \defgroup xml XML handling.
 * \ingroup core
 */

/**
 * \defgroup corexml XML-Core RIP interface.
 * \ingroup xml
 * \{ */

/** The XML sub-system within the RIP. There is only one XML sub-system
   context within the RIP. */
extern xmlGContext *core_xml_subsystem ;

extern hqn_uri_context_t *core_uri_context ;

/**
 * \brief Memory pool for xml allocations.
 *
 * The lifetime of this memory pool is per xmlexec. Recursive calls to
 * xmlexec will use the same memory pool.
 */
extern mm_pool_t mm_xml_pool;

/** Used for plugging various XML layers. */
extern
xmlGMemoryHandler xmlexec_memory_handlers ;

typedef uint32 xml_contextid_t ;

/**
 * \brief A generic document context.
 *
 * This is an opaque type to CORExml. The implementation(s) of this type are
 * private to its clients.
 */
typedef struct xmlDocumentContext xmlDocumentContext ;

/**
 * \brief Callback to create a document context.
 *
 * This gets called when a recognised namespace is found.
 */
typedef Bool (*XMLDocContextCreate)(
      /*@null@*/ /*@in@*/
      struct OBJECT *params,
      /*@null@*/ /*@in@*/
      FILELIST *flptr,
      /*@notnull@*/ /*@out@*/
      xmlDocumentContext **pp_doc_context,
      /*@notnull@*/ /*@in@*/
      xmlGFilterChain *filter_chain) ;

/**
 * \brief Callback to destroy the document context.
 *
 * This gets called when xmlexec is about to exit.
 */
typedef void (*XMLDocContextDestroy)(
      /*@notnull@*/ /*@out@*/
      xmlDocumentContext **p_doc_context) ;


extern
xmlDocumentContext* xml_get_current_doc_context(void) ;

/* ============================================================================
 *
 * ============================================================================
 */

/**
 * Opaque type definition.
 */
typedef struct XMLExecContext XMLExecContext;

/**
 * \brief The various xml_parse_*() methods below require an XML execution
 * context to be in place before they can be called. If they are called within
 * the scope of xmlexec_(), such a context will have been created automatically;
 * otherwise one must be created manually, using this method and the associated
 * xmlexec_context_destroy() when finished.
 *
 * Note that the passed pointer is required purely so that it can be passed in
 * turn to xmlexec_context_destroy(); the context is maintained in global state
 * within the xml system and is not passed to any of the xml_parse_*() methods.
 */
Bool xmlexec_context_create(XMLExecContext** pp_xmlexec_context);

/** \brief Destroy the passed context.
 */
void xmlexec_context_destroy(XMLExecContext** pp_xmlexec_context);

/**
 * \brief Parses the XML content from the specified URI.  If optional is TRUE
 * and the URI cannot be resolved then TRUE is returned, otherwise FALSE is
 * returned.
 *
 * You may need to create a context before calling this function; see comment
 * for xmlexec_context_create().
 */
extern
Bool xml_parse_uri_stream(
      struct hqn_uri_t *uri,
      xmlGFilterChain *filter_chain,
      Bool optional,
      FILELIST **flptr) ;

/**
 * \brief Parses the XML content from the specified URI.
 *
 * You may need to create a context before calling this function; see comment
 * for xmlexec_context_create().
 */
extern
Bool xml_parse_from_uri(
      struct hqn_uri_t *uri,
      xmlGFilterChain *filter_chain,
      FILELIST **flptr) ;
#define xml_parse_from_uri(u, fc, fp) xml_parse_uri_stream((u), (fc), FALSE, (fp))

/**
 * \brief Parses the XML content from the specified URI. If the URI is not accessible,
 * this function returns TRUE. If some other error occurs, FALSE is returned.
 *
 * You may need to create a context before calling this function; see comment
 * for xmlexec_context_create().
 */
extern
Bool xml_parse_from_optional_uri(
      struct hqn_uri_t *uri,
      xmlGFilterChain *filter_chain,
      FILELIST **flptr) ;
#define xml_parse_from_optional_uri(u, fc, fp) xml_parse_uri_stream((u), (fc), TRUE, (fp))

/**
 * Parse XML read from the passed flptr.
 *
 * You may need to create a context before calling this function; see comment
 * for xmlexec_context_create().
 */
extern
Bool xml_parse_stream(
      struct FILELIST* flptr,
      xmlGFilterChain *filter_chain) ;

typedef struct xml_chunk_parser_t xml_chunk_parser_t ;

/**
 * NOTE: You may need to create a context before calling this function; see
 * comment for xmlexec_context_create().
 */
extern
Bool xml_parse_chunk_init(
      FILELIST *flptr,
      xmlGFilterChain *filter_chain,
      xml_chunk_parser_t **chunk_parser) ;

/* If more_data returns FALSE, you should NOT call this function again
   as it will fail. */
extern
Bool xml_parse_chunk(
      xml_chunk_parser_t *chunk_parser,
      Bool *more_data) ;

extern
void xml_parse_chunk_finish(
      xml_chunk_parser_t **chunk_parser,
      Bool error_occurred) ;

/** \brief Hack. This shouldn't be here. This function is used by the XML
    context to remap namespaces. We also need explicit access to it for
    relationships, the Type attribute also needs mapping. */
Bool xml_map_namespace(
      const xmlGIStr *from_uri,
      const xmlGIStr **to_uri) ;

/**
 * If an entry for this namespace already exists the old entry will be replaced.
 *
 * \note The uri gets copied.
 *
 * \note uri can be NULL. Any of the function callbacks can also be
 * NULL.
 */
extern
Bool namespace_recognition_add(
  /*@null@*/ /*@in@*/      const xmlGIStr *uri,
  /*@null@*/               XMLDocContextCreate  f_create_context,
  /*@null@*/               XMLDocContextDestroy f_destroy_context) ;

/** \} */

/* ============================================================================
* Log stripped */
#endif /* !__XML_H__ */
