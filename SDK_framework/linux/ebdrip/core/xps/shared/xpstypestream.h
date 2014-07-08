/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpstypestream.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Public interface for xps content type stream.
 *
 * Allows one to add new default and override mappings to an xps context.
 */

#ifndef __XPSTYPESTREAM_H__
#define __XPSTYPESTREAM_H__

#include "xml.h"
#include "xpsparts.h"

struct SWSTART ; /* from COREinterface */

Bool xps_typestream_swstart(struct SWSTART *params) ;
void xps_typestream_finish(void) ;

Bool xps_types_add_default(
      xmlGFilter *filter,
      xps_extension_t *extension,
      xmlGIStr *mimetype) ;

Bool xps_types_add_override(
      xmlGFilter *filter,
      xps_partname_t *extension,
      xmlGIStr *mimetype) ;

Bool xps_types_get_part_mimetype(
      xmlGFilter *filter,
      xps_partname_t *partname,
      xmlGIStr **mimetype) ;

typedef struct typestream_parser_t typestream_parser_t ;

typestream_parser_t* xps_xml_open_typestream_parser(
      xmlDocumentContext *xps_ctxt,
      hqn_uri_t *package_uri) ;

void xps_xml_close_typestream_parser(
      typestream_parser_t** typestream_parser,
      Bool error_occurred) ;

/* ============================================================================
* Log stripped */
#endif
