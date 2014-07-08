/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlcache.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of an XML cache.
 */

#include "core.h"               /* HQASSERT etc.. */
#include "swerrors.h"           /* error_handler() */
#include "mm.h"
#include "xml.h"                /* xmlG* interface */
#include "xmlcache.h"
#include "lists.h"              /* sll_link_t */

enum {
  START_ELEMENT = 1,
  END_ELEMENT
};

typedef struct XMLCacheElement {
  /* List links - must be first */
  sll_link_t sll;

  int32 elementtype;
  const xmlGIStr *localname;
  const xmlGIStr *prefix;
  const xmlGIStr *uri;
  xmlGAttributes *attrs;
} XMLCacheElement;

struct XMLCache {
  sll_list_t element_stack;
};

Bool xml_cache_create(
      XMLCache **cache)
{
  HQASSERT(cache != NULL, "cache pointer pointer is NULL");

  if ((*cache = mm_alloc(mm_xml_pool, sizeof(XMLCache),
                         MM_ALLOC_CLASS_XML_CACHE) ) == NULL)
    return error_handler(VMERROR);

  SLL_RESET_LIST(&((*cache)->element_stack));
  return TRUE;
}

void xml_cache_destroy(
      XMLCache **cache)
{
  XMLCacheElement *element;

  HQASSERT(cache != NULL, "cache is NULL");
  HQASSERT(*cache != NULL, "cache pointer is NULL");

  while (! SLL_LIST_IS_EMPTY(&((*cache)->element_stack))) {
    element = SLL_GET_HEAD(&((*cache)->element_stack), XMLCacheElement, sll);
    SLL_REMOVE_HEAD(&((*cache)->element_stack));

    switch (element->elementtype) {
    case START_ELEMENT:
      if (element->attrs != NULL)
        xmlg_attributes_destroy(&(element->attrs));
      /*@fallthrough@*/
    case END_ELEMENT:
      HQASSERT(element->localname != NULL, "element name is NULL");
      element->localname = NULL;
      element->prefix = NULL;
      element->uri = NULL;
      break;
    }
    mm_free(mm_xml_pool, element, sizeof(XMLCacheElement));
  }

  mm_free(mm_xml_pool, *cache, sizeof(XMLCache));
  *cache = NULL;
}

static void xml_cache_name_prefix_uri(
      XMLCacheElement *element,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri)
{
  HQASSERT(element != NULL, "element pointer is NULL");
  HQASSERT(localname != NULL, "localname pointer is NULL");

  element->localname = localname;
  element->prefix = prefix;
  element->uri = uri;
}

Bool xml_cache_start_element(
      XMLCache *cache,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  XMLCacheElement *element;
  HQASSERT(cache != NULL, "cache pointer is NULL");

  if ((element = mm_alloc(mm_xml_pool, sizeof(XMLCacheElement),
                          MM_ALLOC_CLASS_XML_CACHE) ) == NULL)
    return error_handler(VMERROR);

  element->elementtype = START_ELEMENT;

  xml_cache_name_prefix_uri(element, localname, prefix, uri);

  if (attrs != NULL)
    xmlg_attributes_reserve(attrs);
  element->attrs = attrs;

  SLL_RESET_LINK(element, sll);
  SLL_ADD_TAIL(&(cache->element_stack), element, sll);

  return TRUE;
}

Bool xml_cache_end_element(
      XMLCache *cache,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri)
{
  XMLCacheElement *element;
  HQASSERT(cache != NULL, "cache pointer is NULL");

  if ((element = mm_alloc(mm_xml_pool, sizeof(XMLCacheElement),
                          MM_ALLOC_CLASS_XML_CACHE) ) == NULL)
    return error_handler(VMERROR);

  element->elementtype = END_ELEMENT;

  xml_cache_name_prefix_uri(element, localname, prefix, uri);
  element->attrs = NULL;

  SLL_RESET_LINK(element, sll);
  SLL_ADD_TAIL(&(cache->element_stack), element, sll);

  return TRUE;
}

Bool xml_cache_execute(
      XMLCache *cache,
      xmlGFilterChain *filter_chain)
{
  XMLCacheElement *element;
  HQASSERT(cache != NULL, "cache pointer is NULL");
  HQASSERT(filter_chain != NULL, "filter_chain pointer is NULL");

  if (SLL_LIST_IS_EMPTY(&(cache->element_stack)))
    return TRUE;

  element = SLL_GET_HEAD(&(cache->element_stack), XMLCacheElement, sll);
  do {
    HQASSERT(element != NULL, "element is NULL");
    switch (element->elementtype) {
    case START_ELEMENT:
      /* dispatch the start callback */
      if (! xmlg_fc_execute_start_element(filter_chain,
                                          element->localname,
                                          element->prefix,
                                          element->uri,
                                          element->attrs))
        return error_handler(UNDEFINED);
      break;
    case END_ELEMENT:
      /* dispatch the end callback */
      if (! xmlg_fc_execute_end_element(filter_chain,
                                        element->localname,
                                        element->prefix,
                                        element->uri, TRUE))
        return error_handler(UNDEFINED);
      break;
    } /* end switch */

    element = SLL_GET_NEXT(element, XMLCacheElement, sll);
  } while (element != NULL);

  return TRUE;
}

/* ============================================================================
* Log stripped */
