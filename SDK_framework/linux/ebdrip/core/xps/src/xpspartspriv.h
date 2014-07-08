/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:xpspartspriv.h(EBDSDK_P.1) $
 * $Id: src:xpspartspriv.h,v 1.10.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private interface to Metro parts and extensions.
 */

#ifndef __XPSPARTSPRIV_H__
#define __XPSPARTSPRIV_H__

#include "objnamer.h"

#include "xpspriv.h"

#define XMLPART_CTXT_NAME "XMLPART_CTXT_NAME"

/**
 * \brief Create a new XML processing part context.
 *
 * \param xps_ctxt Pointer to the document context.
 * \param part_name Pointer to the part name.
 * \param rels_block Location to store relationships associated with this
 *        part. For _rels/.rels, this will be the package relationships block.
 *        When processing other .rels parts, this will be the source part.
 * \param part_ctxt Pointer to new part context.
 *
 * \retval TRUE Success.
 * \retval FALSE Failure.
 */
extern
Bool xps_xml_part_context_new(
      xmlDocumentContext *xps_ctxt,
      xps_partname_t *part_name,
      xpsRelationshipsBlock *rels_block,
      uint32 relationships_to_process,
      Bool need_new_relationships_parser,
      struct xpsXmlPartContext **part_ctxt) ;

extern
Bool xps_xml_part_context_free(
      struct xpsXmlPartContext **part_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
