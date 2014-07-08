/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!export:xmlcache.h(EBDSDK_P.1) $
 * $Id: export:xmlcache.h,v 1.12.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to Cache XML start/end elements.
 */

#ifndef __XMLCACHE_H__
#define __XMLCACHE_H__

#include "xmlg.h"

typedef struct XMLCache XMLCache;

/**
 * \brief Create an XML cache instance.
 * \param cache Handle to a cache pointer.
 *
 * \retval TRUE success
 * \retval FALSE failure
 */
extern
Bool xml_cache_create(
      /*@out@*/ /*@notnull@*/
      XMLCache **cache);

/**
 * \brief Destroy an XML cache instance freeing all memory.
 * \param cache Handle to a valid cache pointer.
 */
extern
void xml_cache_destroy(
      /*@in@*/ /*@notnull@*/
      XMLCache **cache);

/**
 * \brief Cache a start element node.
 * \param cache Pointer to a valid cache.
 * \param localname Name of start element.
 * \param prefix Prefix of start element.
 * \param uri Optional URI of start element.
 * \param attrs Optional attributes of start element.
 */
extern
Bool xml_cache_start_element(
      /*@in@*/ /*@notnull@*/
      XMLCache *cache,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@null@*/
      xmlGAttributes *attrs);

/**
 * \brief Cache an end element node.
 * \param cache Pointer to a valid cache.
 * \param localname Name of end element.
 * \param prefix Prefix of end element.
 * \param uri Optional URI of end element.
 *
 * \retval TRUE success
 * \retval FALSE failure
 */
extern
Bool xml_cache_end_element(
      /*@in@*/ /*@notnull@*/
      XMLCache *cache,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri);

/**
 * \brief Execute an XML cache on an XML parse handler.
 *
 * XML Caches are executed on valid XML handlers. This is so that the start/end
 * callbacks which are registered get called and should the error undo stack
 * be enabled, the executed XML cache will follow the error stack semantics.
 *
 * \param cache Pointer to a valid cache.
 * \param filter_chain XML filter chain to execute this XML on.
 *
 * \retval TRUE success.
 * \retval FALSE failure. This will occur if the XML callback caused an error.
 */
extern
Bool xml_cache_execute(
      /*@in@*/ /*@notnull@*/
      XMLCache *cache,
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain);

/* ============================================================================
* Log stripped */
#endif /* !__XMLCACHE_H__ */
