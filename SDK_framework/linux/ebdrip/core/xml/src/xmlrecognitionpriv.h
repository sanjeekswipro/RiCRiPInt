/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlrecognitionpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private interface for namespace recognition filter.
 */

#ifndef __XMLRECOGNITIONPRIV_H__
#define __XMLRECOGNITIONPRIV_H__ (1)

#include "xml.h"

struct SWSTART ; /* from COREinterface */

Bool namespace_recognition_swstart(struct SWSTART *params) ;

void namespace_recognition_finish(void) ;

extern
Bool recognition_xml_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter) ;

extern
Bool recognition_xml_filter_dispose(
      xmlGFilter **filter) ;

/* ============================================================================
* Log stripped */
#endif
