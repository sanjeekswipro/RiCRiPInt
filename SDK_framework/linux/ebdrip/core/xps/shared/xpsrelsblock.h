/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpsrelsblock.h(EBDSDK_P.1) $
 * $Id: shared:xpsrelsblock.h,v 1.7.10.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for storing relationship blocks.
 */

#ifndef __XPSRELSBLOCK_H__
#define __XPSRELSBLOCK_H__

#include "xml.h"
#include "xpsparts.h"

typedef struct xpsRelationshipsBlock xpsRelationshipsBlock ;

typedef struct xpsRelationship xpsRelationship ;

extern
Bool xps_create_relationship_block(
      struct xpsRelationshipsBlock **rels_block,
      xmlDocumentContext *xps_ctxt,
      int32 relationships_to_process,
      xps_partname_t *partname,
      Bool new_parse_instance) ;

extern
Bool xps_destroy_relationship_block(
      xpsRelationshipsBlock **rels_block) ;

extern
Bool xps_add_relationship(
      xpsRelationshipsBlock *rels_block,
      xmlGIStr *id,
      xmlGIStr *type,
      xps_partname_t *target,
      xmlGIStr *targetmode) ;

extern
Bool xps_lookup_relationship_type(
      xpsRelationshipsBlock *rels_block,
      xmlGIStr *type,
      xpsRelationship **relationship,
      Bool parse_more) ;

extern
Bool xps_lookup_relationship_target_type(
      xpsRelationshipsBlock *rels_block,
      xps_partname_t *target,
      xmlGIStr *type,
      xpsRelationship **relationship,
      Bool parse_more) ;

extern
xps_partname_t *xps_rels_get_target(
      xpsRelationship *relationship) ;

/* ============================================================================
* Log stripped */
#endif
