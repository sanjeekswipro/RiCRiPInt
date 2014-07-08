/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!package:src:package.c(EBDSDK_P.1) $
 * $Id: package:src:package.c,v 1.14.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation to register XPS package XML callbacks.
 */

#include "core.h"
#include "xml.h"
#include "package.h"
#include "packagepriv.h"

/*=============================================================================
 * Register functions
 *=============================================================================
 */

Bool xmlcb_register_functs_package(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter)
{
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");
  HQASSERT(filter != NULL, "filter is NULL");

  return (
    xmlcb_register_funcs_xps_contenttype(xps_ctxt, filter) &&
    xmlcb_register_funcs_xps_relationships(xps_ctxt, filter) &&
    xmlcb_register_funcs_xps_coreproperties(xps_ctxt, filter)
  ) ;
}

/* ============================================================================
* Log stripped */
