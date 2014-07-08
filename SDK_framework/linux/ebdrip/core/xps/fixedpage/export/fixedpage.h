/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:export:fixedpage.h(EBDSDK_P.1) $
 * $Id: fixedpage:export:fixedpage.h,v 1.15.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Public interface to register eDoc fixedpage XML callback functions.
 */

#ifndef __FIXEDPAGE_H__
#define __FIXEDPAGE_H__

/** \defgroup fixedpage XPS FixedPage XML callback functions
    \ingroup xps */
/** \{ */

#include "xml.h"
#include "hqunicode.h"

extern Bool xmlcb_register_functs_fixedpage(
      xmlDocumentContext *xps_context,
      xmlGFilter *filter) ;

Bool xps_brush_execute(
      xmlGFilterChain *filter_chain,
      utf8_buffer *name) ;

/** Reset the direct image state and disable the optimisation. */
void reset_direct_image(xmlDocumentContext *xps_ctxt);

/** \} */

#endif /* __FIXEDPAGE_H__ */

/* ============================================================================
* Log stripped */
