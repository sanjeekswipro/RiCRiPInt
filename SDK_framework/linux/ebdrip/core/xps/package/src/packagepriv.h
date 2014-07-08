/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!package:src:packagepriv.h(EBDSDK_P.1) $
 * $Id: package:src:packagepriv.h,v 1.13.10.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private interface to register specific package XML callbacks.
 */

#ifndef __PACKAGEPRIV_H__
#define __PACKAGEPRIV_H__

#include "xml.h"

extern Bool xmlcb_register_funcs_xps_contenttype(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter);

extern Bool xmlcb_register_funcs_xps_relationships(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter);

extern Bool xmlcb_register_funcs_xps_versioning(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter);

extern Bool xmlcb_register_funcs_xps_coreproperties(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter);

/* ============================================================================
* Log stripped */
#endif
