/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpsresblock.h(EBDSDK_P.1) $
 * $Id: shared:xpsresblock.h,v 1.24.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to xps resource block.
 */

#ifndef __XPSRESBLOCK_H__
#define __XPSRESBLOCK_H__

#include "xmlcache.h"

typedef struct xpsResourceReference xpsResourceReference ;

/** \brief Opaque definition for linked chain of resource blocks. */
typedef struct xpsResourceBlock {
  /* List links - must be first */
  sll_link_t sll ;

  /* Unique id for this resource block. Starts from zero per XPS
     page. */
  uint32 uid ;

  /* Used for tracking the correct end ResourceDictionary element. */
  uint32 depth ;

  /* Remote resources use the base of the remote resource, not the
     referring part. */
  hqn_uri_t *base ;

  /* Maps resource name to an xpsResource */
  XmlStrHashTable *resources ;

  OBJECT_NAME_MEMBER

} xpsResourceBlock ;


typedef struct xpsResource xpsResource ;

struct xpsResourceReference {
  uint8 *refname ;
  int32 reflen ;
  uint32 depth ;
  xpsResource *resource ;
  xmlGIStr *elementname ;
  struct xpsResourceReference *next ;
} ;

/** \brief Destroy a resource block and all XML caches. */
extern
void xps_resblock_destroy(
      xpsResourceBlock **resblock) ;

/** \brief Start adding a new XML cache to a resource block. */
extern
Bool xps_resblock_add_resource(
      const xpsResourceBlock *resblock,
      const uint8 *resourcename,
      uint32 resourcenamelen,
      xpsResource **resource,
      Bool is_remote,
      uint32 resource_uid) ;

/** \brief Look up an XML cache in a resource block by name. */
extern
Bool xps_resblock_get_resource(
      const xpsResourceBlock *resblock,
      xpsResourceReference *reference,
      xpsResource **resource) ;

extern
Bool xps_resource_is_remote(
      const xpsResource *resource) ;

extern
Bool xps_resource_is_executing(
      const xpsResource *resource) ;

extern
uint32 xps_resource_uid(
      const xpsResource *resource) ;

/** \brief Re-open the top existing resource block for definitions. */
extern
void xps_resources_append(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix) ;

/* The scope can be used to force a resource declaration to be
   attached to the FixedPage resource block. This obviously lasts the
   life time of the page itself.*/
enum { RES_SCOPE_FIXEDPAGE, RES_SCOPE_WHERE_DECLARED } ;

/** \brief Capture XML callbacks into XML caches until end callback is
    seen. */
extern
Bool xps_resources_start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      int32 scope_required) ;

/** \brief Stop capture of XML callbacks into XML caches. */
extern
Bool xps_resources_end(
      xmlGFilter *filter,
      Bool success) ;

extern
void xps_resource_destroy(
      xpsResource **resource) ;

extern
Bool xps_resource_execute(
      xmlGFilterChain *filter_chain,
      xpsResource *resource,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGAttributes *attrs) ;

extern
const xpsResourceBlock* xps_resource_get_resblock(
      xpsResource *resource) ;

extern
const xpsResourceBlock* xps_resource_get_latest_resblock(
      xpsResource *resource) ;

/** \brief Obtain depth of resource block. */
extern
uint32 xps_resblock_depth(
      const xpsResourceBlock *resblock) ;

extern
uint32 xps_resblock_uid(
      const xpsResourceBlock *resblock) ;

extern
Bool xps_resource_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      xmlDocumentContext *xps_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
