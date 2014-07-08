/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:resblock.c(EBDSDK_P.1) $
 * $Id: src:resblock.c,v 1.47.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of xps resource block.
 */

#include "core.h"
#include "hqmemcpy.h"
#include "mmcompat.h"   /* mm_alloc_with_header etc.. */
#include "lists.h"      /* sll_list_t */
#include "swerrors.h"
#include "objnamer.h"

#include "xml.h"

#include "xpsscan.h"
#include "xpspriv.h"
#include "xpsresblock.h"
#include "xpspartspriv.h"

#include "namedef_.h"

/* Smallish prime.
 * Do not expect that many resource names in a single resource block.
 */
/** \todo TODO: Hash size and function will need profiling.
 */
#define RESOURCEBLOCK_HASH_SIZE 109

#define XPSRESOURCEBLOCK_NAME "XPSRESOURCEBLOCK_NAME"

struct xpsResource {
  /* An element within a resource may never reference itself. Before
     executing a resource, we check that it is not already running
     which would imply a recursive reference. */
  Bool is_executing ;

  /* Was this resource defined in a remote resource? We use this to
     disallow remote resources referencing ancestor resource
     dictionaries. */
  Bool is_remote ;

  /* Unique id inherited from resource block definition. Its unlikely
     that we will have 2^32 resource dictionaries in a single XPS
     page. */
  uint32 uid ;

  /* When resources are executing, we keep a stack of executing
     resources so we can scope resource references. */
  xpsResource *prev_executing ;

  /* The resource block which this resource belongs to. */
  const xpsResourceBlock *resblock ;

  /* We keep a pointer to the latest resource block defined when
     executing a resource so we can track all subsequent resource
     definitions which will be in scope from any resource
     references. */
  xpsResourceBlock *latest_resblock ;

  /* The XML which represents the cached resource. */
  XMLCache *cache ;
} ;

/* ============================================================================
 */

void xps_resource_destroy(
      xpsResource **resource)
{
  xpsResource *old_resource ;

  HQASSERT(resource != NULL, "resource is NULL") ;
  old_resource = *resource ;
  HQASSERT(old_resource != NULL, "old_resource is NULL") ;

  if (old_resource->cache != NULL) {
    xml_cache_destroy(&old_resource->cache) ;
  }
  mm_free(mm_xml_pool, old_resource, sizeof(xpsResource)) ;

  *resource = NULL ;
}

Bool xps_resource_create(
      xpsResource **resource,
      const xpsResourceBlock *resblock,
      Bool is_remote,
      uint32 resource_uid)
{
  xpsResource *new_resource ;

  HQASSERT(resource != NULL, "resource is NULL") ;
  HQASSERT(resblock != NULL, "resblock is NULL") ;
  HQASSERT(resource_uid > 0, "resource_uid is not greater than zero") ;
  *resource = NULL ;

  if ((new_resource = mm_alloc(mm_xml_pool, sizeof(xpsResource),
                               MM_ALLOC_CLASS_XPS_RESOURCE)) == NULL)
    return error_handler(VMERROR) ;

  new_resource->is_executing = FALSE ;
  new_resource->is_remote = is_remote ;
  new_resource->prev_executing = NULL ;
  new_resource->resblock = resblock ;
  new_resource->latest_resblock = NULL ;
  new_resource->uid = resource_uid ;
  new_resource->cache = NULL ;

  if (! xml_cache_create(&new_resource->cache)) {
    xps_resource_destroy(&new_resource) ;
    return FALSE ;
  }

  *resource = new_resource ;

  return TRUE ;
}

Bool xps_resource_execute(
      xmlGFilterChain *filter_chain,
      xpsResource *resource,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  Bool status ;
  xpsXmlPartContext *xmlpart_ctxt ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  HQASSERT(resource != NULL, "resource is NULL") ;
  HQASSERT(localname != NULL, "localname is NULL") ;

  if (resource->is_executing)
    detail_error_handler(UNDEFINED,
                         "Recursive resource reference found.") ;

  resource->is_executing = TRUE ;
  resource->prev_executing = xmlpart_ctxt->executing_stack ;
  resource->latest_resblock = SLL_GET_HEAD(&(xmlpart_ctxt->resourceblock_stack), xpsResourceBlock, sll) ;
  HQASSERT(resource->latest_resblock != NULL, "latest_resblock is NULL") ;
  xmlpart_ctxt->executing_stack = resource ;

  HQASSERT(resource->resblock != NULL, "resource resblock is NULL") ;
  HQASSERT(resource->resblock->base != NULL, "resource resblock base is NULL") ;

  /* Remember that xmlG will handle de-scoping the base when it sees
     the appropriate end element. */

  status = xmlg_fc_execute_start_element(filter_chain, localname, /* prefix */ NULL, uri, attrs) &&
           xmlg_fc_set_base_uri(filter_chain, resource->resblock->base) &&
           xml_cache_execute(resource->cache, filter_chain) &&
           xmlg_fc_execute_end_element(filter_chain, localname, /* prefix */ NULL, uri, TRUE) ;

  xmlpart_ctxt->executing_stack = resource->prev_executing ;
  resource->prev_executing = NULL ;
  resource->latest_resblock = NULL ;
  resource->is_executing = FALSE ;

  return status ;
}

const xpsResourceBlock* xps_resource_get_resblock(
      xpsResource *resource)
{
  HQASSERT(resource != NULL, "resource is NULL") ;
  return resource->resblock ;
}

const xpsResourceBlock* xps_resource_get_latest_resblock(
      xpsResource *resource)
{
  HQASSERT(resource != NULL, "resource is NULL") ;
  return resource->latest_resblock ;
}

/* ============================================================================
 */
static void * xml_resource_malloc( size_t size)
{
  void *alloc;
  alloc = mm_alloc_with_header(mm_xml_pool, (mm_size_t)size,
                               MM_ALLOC_CLASS_XPS_RESOURCE) ;
  if (alloc == NULL)
    (void) error_handler(VMERROR);

  return alloc;
}

static void xml_hash_free(void *memPtr)
{
  mm_free_with_header(mm_xml_pool, memPtr) ;
}

/**
 * \brief De-allocates the XMLCache which is the payload data in the
 * resource hash table.
 *
 * \param mem_ptr points to an XMLCache
 */
static void PayLoadFree(
    void *mem_ptr)
{
  xpsResource *resource ;
  HQASSERT(mem_ptr != NULL, "mem_ptr is NULL") ;
  resource = mem_ptr ;
  xps_resource_destroy(&resource) ;
}

void xps_resblock_destroy(
    xpsResourceBlock **resblock)
{
  xpsResourceBlock *old_resblock ;
  HQASSERT(resblock != NULL, "resblock pointer is NULL") ;
  old_resblock = *resblock ;
  HQASSERT(old_resblock != NULL, "old_resblock is NULL") ;

  VERIFY_OBJECT(old_resblock, XPSRESOURCEBLOCK_NAME) ;
  UNNAME_OBJECT(old_resblock) ;

  if (old_resblock->base != NULL) {
    hqn_uri_free(&old_resblock->base) ;
  }

  if (old_resblock->resources != NULL) {
    xmlstr_hash_destroy(&old_resblock->resources) ;
  }

  mm_free(mm_xml_pool, old_resblock, sizeof(xpsResourceBlock)) ;

  *resblock = NULL ;
}

static Bool xps_resblock_create(
      xpsResourceBlock **resblock,
      uint32 depth,
      uint32 resourceblock_id,
      hqn_uri_t *base)
{
  xpsResourceBlock *new_resblock ;
  HQASSERT(resblock != NULL, "resblock pointer is NULL") ;
  HQASSERT(base != NULL, "base is NULL") ;

  *resblock = NULL ;

  if ((new_resblock = mm_alloc(mm_xml_pool, sizeof(xpsResourceBlock),
                               MM_ALLOC_CLASS_XPS_RESOURCE)) == NULL)
    return error_handler(VMERROR) ;

  SLL_RESET_LINK(new_resblock, sll) ;
  NAME_OBJECT(new_resblock, XPSRESOURCEBLOCK_NAME) ;
  new_resblock->depth = depth ;
  new_resblock->uid = resourceblock_id ;
  new_resblock->base = NULL ;
  new_resblock->resources = NULL ;

  if (! hqn_uri_copy(core_uri_context, base, &new_resblock->base)) {
    xps_resblock_destroy(&new_resblock) ;
    return error_handler(VMERROR) ;
  }

  new_resblock->resources = xmlstr_hash_create(RESOURCEBLOCK_HASH_SIZE,
                                               PayLoadFree,
                                               xml_resource_malloc,
                                               xml_hash_free,
                                               NULL /* default hash */ ) ;

  if (new_resblock->resources == NULL) {
    xps_resblock_destroy(&new_resblock) ;
    return FALSE ;
  }

  *resblock = new_resblock ;

  return TRUE ;
}

Bool xps_resblock_add_resource(
      const xpsResourceBlock *resblock,
      const uint8 *resourcename,
      uint32 resourcenamelen,
      xpsResource **resource,
      Bool is_remote,
      uint32 resource_uid)
{
  xpsResource *new_resource, *old_resource ;
  void *val ;

  VERIFY_OBJECT(resblock, XPSRESOURCEBLOCK_NAME) ;
  HQASSERT(resourcename != NULL, "resourcename is NULL") ;
  HQASSERT(resource != NULL, "resource is NULL") ;

  if (! xps_resource_create(&new_resource, resblock, is_remote, resource_uid))
    return FALSE ;

  HQASSERT(resblock->resources != NULL, "resblock resources is NULL") ;

  if (! xmlstr_hash_add(resblock->resources, resourcename, resourcenamelen, new_resource, &val)) {
    xps_resource_destroy(&new_resource) ;
    return FALSE ;
  }
  old_resource = val ;

  /* A resource with this name already exists in the resource block. */
  if (old_resource != NULL) {
    /* do no deallocate the inserted cache, this will be freed when the
     * resource block gets destroyed.
     */
    xps_resource_destroy(&old_resource) ;

    return detailf_error_handler(SYNTAXERROR,
             "Duplicate Key=\"%.*s\" within <ResourceDictionary>.",
             resourcenamelen, resourcename) ;
  }

  *resource = new_resource ;
  return TRUE ;
}

Bool xps_resblock_get_resource(
      const xpsResourceBlock *resblock,
      xpsResourceReference *reference,
      xpsResource **resource)
{
  void *val = NULL ;
  Bool res = FALSE ;

  HQASSERT(resblock != NULL, "resblock is NULL") ;
  VERIFY_OBJECT(resblock, XPSRESOURCEBLOCK_NAME) ;
  HQASSERT(reference != NULL, "reference is NULL") ;
  HQASSERT(resource != NULL, "resource is NULL") ;

  /* The resblock depth is the element depth of the ResourceDictionary
     element. This always exists within either a FixedPage.Resources
     or Canvas.Resources element. So the depth -1 is the depth of one
     of those elements. */

  if (reference->depth >= (resblock->depth - 1)) {
    res = xmlstr_hash_get(resblock->resources, reference->refname, reference->reflen, &val) ;
  }

  *resource = val ;
  return res ;
}

uint32 xps_resblock_depth(
      const xpsResourceBlock *resblock)
{
  HQASSERT(resblock != NULL, "resblock is NULL") ;
  VERIFY_OBJECT(resblock, XPSRESOURCEBLOCK_NAME) ;
  return resblock->depth ;
}

uint32 xps_resblock_uid(
      const xpsResourceBlock *resblock)
{
  HQASSERT(resblock != NULL, "resblock is NULL") ;
  VERIFY_OBJECT(resblock, XPSRESOURCEBLOCK_NAME) ;
  return resblock->uid ;
}

/* ============================================================================
 */
Bool xps_resource_is_remote(
      const xpsResource *resource)
{
  HQASSERT(resource != NULL, "resource is NULL") ;
  return resource->is_remote ;
}

Bool xps_resource_is_executing(
      const xpsResource *resource)
{
  HQASSERT(resource != NULL, "resource is NULL") ;
  return resource->is_executing ;
}

uint32 xps_resource_uid(
      const xpsResource *resource)
{
  HQASSERT(resource != NULL, "resource is NULL") ;
  return resource->uid ;
}

void xps_resources_append(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;

  UNUSED_PARAM( const xmlGIStr*, localname ) ;
  UNUSED_PARAM( const xmlGIStr*, uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;
  HQASSERT(localname != NULL, "localname is NULL") ;

  /* Turn on defining resources. */
  xmlpart_ctxt->defining_resources = TRUE ;
  xmlpart_ctxt->active_res_depth = 0 ;
}

Bool xps_resources_start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      int32 scope_required)
{
  xmlGFilterChain *filter_chain ;
  xmlDocumentContext *xps_ctxt ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xpsResourceBlock *resblock ;
  hqn_uri_t *base ;
  uint32 depth ;

  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;

  /* If we want a root scope, we create the resource as if though it
     was attached to a fixed page. */
  if (scope_required == RES_SCOPE_FIXEDPAGE) {
    depth = 1 ;
  } else {
    HQASSERT(scope_required == RES_SCOPE_WHERE_DECLARED,
             "invalid scope_required") ;
    if (xps_ctxt->remote_resource) {
      depth = xps_ctxt->remote_resource_depth ;
    } else {
      depth = xmlg_get_element_depth(filter) ;
    }
  }

  xps_ctxt->resourceblock_uid++ ;
  HQASSERT(xps_ctxt->resourceblock_uid != 0,
           "overflowed resource block id") ;

  base = xmlg_fc_get_base_uri(filter_chain) ;
  HQASSERT(base != NULL, "base is NULL") ;

  if (! xps_resblock_create(&resblock, depth, xps_ctxt->resourceblock_uid, base))
    return FALSE ;

  xps_resources_append(filter, localname, uri);

  if (xps_ctxt->remote_resource) {
    HQASSERT(xps_ctxt->remote_resource_source != NULL,
             "remote resource source is NULL") ;

    /* Link in new resource block into Source part now that we've
       succeeded. */
    SLL_ADD_HEAD(&(xps_ctxt->remote_resource_source->resourceblock_stack), resblock, sll) ;
  } else {
    /* Link in new resource block now that we've succeeded. */
    SLL_ADD_HEAD(&(xmlpart_ctxt->resourceblock_stack), resblock, sll) ;
  }

  return TRUE ;
}

Bool xps_resources_end(
      xmlGFilter *filter,
      Bool success)
{
  xmlGFilterChain *filter_chain ;
  xmlDocumentContext *xps_ctxt ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xpsResourceBlock *resblock ;
  uint32 depth ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;

  if (! xmlpart_ctxt->defining_resources)
    return success ;

  depth = xmlg_get_element_depth(filter) ;

  /* Turn off defining resources. */
  xmlpart_ctxt->defining_resources = FALSE ;
  HQASSERT(xmlpart_ctxt->active_res_depth == 0, "Resource depth is not zero") ;
  xmlpart_ctxt->active_resource = NULL ;

  /* Only deallocate resource blocks when we are dealing with an
   * error. This is because resource blocks live beyond the scope of
   * their end element callback. Also, if we are loading a remote
   * resource, the resource will have been loaded onto the source
   * part, so its cleanup will do the deallocation.
   */
  if (! success && ! xps_ctxt->remote_resource) {
    HQASSERT(! SLL_LIST_IS_EMPTY(&(xmlpart_ctxt->resourceblock_stack)),
             "resource stack is empty") ;

    /* destroy all resource blocks at this level */
    while ((resblock = SLL_GET_HEAD(&(xmlpart_ctxt->resourceblock_stack), xpsResourceBlock, sll)) != NULL) {
      HQASSERT(resblock->depth <= depth,
               "resource depth deeper than current depth") ;
      if (resblock->depth == depth) {
        SLL_REMOVE_HEAD(&(xmlpart_ctxt->resourceblock_stack)) ;
        xps_resblock_destroy(&resblock) ;
      } else {
        break ;
      }
    }
  }

  return success ;
}

Bool xps_resource_reference(xmlGFilter *filter,
                            xmlGIStr *elementname,
                            xmlGIStr *attrlocalname,
                            const uint8 *refname, uint32 reflen)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;

  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;

  UNUSED_PARAM( xmlGIStr *, attrlocalname) ;

  HQASSERT(elementname != NULL, "No substitute element name") ;
  HQASSERT(refname != NULL, "No resource reference name") ;
  HQASSERT(reflen > 0, "Resource reference length zero") ;

  {
    xpsResourceReference *reference ;

    if ( (reference = mm_alloc(mm_xml_pool, sizeof(xpsResourceReference),
                               MM_ALLOC_CLASS_XPS_RESOURCE)) == NULL)
      return error_handler(VMERROR) ;

    /* We bind the cache when processing complex properties - this
       allows a Canvas to reference its own resource dictionary. */
    reference->resource = NULL ;
    reference->elementname = elementname ;
    reference->reflen = reflen ;
    reference->depth = xmlg_get_element_depth(filter) ;

    if ( (reference->refname = mm_alloc(mm_xml_pool, reflen,
                                        MM_ALLOC_CLASS_XPS_RESOURCE)) == NULL) {
      mm_free(mm_xml_pool, reference, sizeof(xpsResourceReference)) ;
      return error_handler(VMERROR) ;
    }
    HqMemCpy(reference->refname, refname, reflen) ;

    /* the xps_commit_resource_ref links in the newly allocated
       reference */
    if (! xps_commit_resource_ref(filter, elementname, reference)) {
      mm_free(mm_xml_pool, reference->refname, reference->reflen) ;
      mm_free(mm_xml_pool, reference, sizeof(xpsResourceReference)) ;
      return FALSE ;
    }
  }

  return TRUE ;
}

/* ============================================================================
 * Pre XPS resources filter.
 * ============================================================================
 */
static Bool xps_resource_start_element_cb (
       xmlGFilter *filter,
       const xmlGIStr *localname,
       const xmlGIStr *prefix,
       const xmlGIStr *uri,
       xmlGAttributes *attrs)
{
  xmlGFilterChain *filter_chain ;
  xmlDocumentContext *xps_ctxt ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xpsResourceBlock *curr_resblock ;
  xpsResource *resource = NULL ;
  static utf8_buffer key, name ;
  static Bool key_set, name_set ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Key), XML_INTERN(ns_xps_2005_06_resourcedictionary_key), &key_set, xps_convert_Key, &key },
    XML_ATTRIBUTE_MATCH_END
  } ;
  static XML_ATTRIBUTE_MATCH match_name[] = {
    { XML_INTERN(Name), NULL, &name_set, xps_convert_ST_Name, &name },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGAttributes* , attrs ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;

  /* Are we defining resources? */
  if (xmlpart_ctxt->defining_resources) {
    if (xps_ctxt->remote_resource) {
      curr_resblock = SLL_GET_HEAD(&(xps_ctxt->remote_resource_source->resourceblock_stack),
                                   xpsResourceBlock, sll) ;
    } else {
      curr_resblock = SLL_GET_HEAD(&(xmlpart_ctxt->resourceblock_stack),
                                   xpsResourceBlock, sll) ;
    }

    HQASSERT(curr_resblock != NULL, "resource block is NULL") ;

    /* The Name attribute MUST NOT be specified on any children of a
       <ResourceDictionary> element [M9.10]. */

    if (! xmlg_attributes_match(filter, localname, uri, attrs, match_name, FALSE))
      return error_handler(UNDEFINED) ; /* must be a scan error */

    if (name_set && ! xmlpart_ctxt->defining_brush_resource)
      return detail_error_handler(SYNTAXERROR,
                 "Name attribute specified on child element of <ResourceDictionary>.") ;

    /* We have a the first element in a ResourceDictionary, it MUST
       have a key specified. */
    if (xmlpart_ctxt->active_res_depth == 0) {
      if (! xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE))
        return error_handler(UNDEFINED) ; /* must be a scan error */

      if (key_set) {
        if (! xps_resblock_add_resource(curr_resblock, key.codeunits,
                                        key.unitlength, &resource,
                                        xps_ctxt->remote_resource,
                                        curr_resblock->uid ))
          return FALSE ;

        /* Remove the Key attribute. */
        xmlg_attributes_remove(attrs, XML_INTERN(Key), XML_INTERN(ns_xps_2005_06_resourcedictionary_key)) ;
        xmlpart_ctxt->active_resource = resource ;
      } else {
        if (! xmlpart_ctxt->defining_brush_resource)
          return detail_error_handler(SYNTAXERROR,
                 "Resource is missing a Key attribute or Key namespace is incorrect.") ;
      }
    } else {
      if (xmlpart_ctxt->active_resource == NULL) {
        /* Test that we have an active resource cache. This can happen if you put
         * XML in a resource definition block which is wrong. Example:
         *   <ResourceDictionary>
         *     <BlaaBlaaBlaa/>
         *   </ResourceDictionary>
         *
         * Note that the namespace could be wrong.
         */
        return detail_error_handler(SYNTAXERROR,
               "Resource is missing a Key attribute or Key namespace is incorrect.") ;
      }
    }

    HQASSERT(xmlpart_ctxt->active_resource != NULL,
             "active resource cache is NULL") ;

    /* Cache this XML. */
    if (! xml_cache_start_element(xmlpart_ctxt->active_resource->cache,
                                  localname, prefix, uri, attrs))
      return FALSE ;

    xmlpart_ctxt->active_res_depth++ ;

    return XMLG_RESULT_HANDLED ;
  }

  return TRUE ;
}

static Bool xps_resource_end_element_cb (
       xmlGFilter *filter,
       const xmlGIStr *localname,
       const xmlGIStr *prefix,
       const xmlGIStr *uri,
       Bool success)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;

  if (xmlpart_ctxt->defining_resources) {
    /* Protection against picking up the end resource element. */
    if (xmlpart_ctxt->active_res_depth != 0) {
      /* In the event of an error, there is no point caching more end elements
       * in the cache.
       */
      if (success) {
        HQASSERT(xmlpart_ctxt->active_resource != NULL,
                 "Active resource cache is NULL") ;

        /* Cache this XML. */
        if (! xml_cache_end_element(xmlpart_ctxt->active_resource->cache,
                                    localname, prefix, uri)) {
          success = FALSE ;
        }
      }
      xmlpart_ctxt->active_res_depth-- ;
    } else {
      HQASSERT(localname == XML_INTERN(ResourceDictionary) ||
               localname == XML_INTERN(VisualBrush_Visual),
               "Skipping non end resource element") ;

      success = xps_resources_end(filter, success) ;

      /* NOTE: calls will now be forwared. */
      return success ;
    }

    if (success)
      return XMLG_RESULT_HANDLED ;
  }

  return success ;
}

Bool xps_resource_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      xmlDocumentContext *xps_ctxt)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  *filter = NULL ;

  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, xps_ctxt,
                           NULL /* no dispose callback */))
    return error_handler(UNDEFINED) ;

  xmlg_set_validity_error_cb(new_filter, xps_validity_error_cb) ;
  xmlg_set_user_error_cb(new_filter, xps_user_error_cb) ;

  /* watch all elements */
  if (! xmlg_register_start_element_cb(new_filter, NULL, NULL, /* all elements */
                                       xps_resource_start_element_cb) ||
      ! xmlg_register_end_element_cb(new_filter, NULL, NULL, /* all elements */
                                     xps_resource_end_element_cb)) {
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  *filter = new_filter ;

  return TRUE ;
}

/* ============================================================================
* Log stripped */
