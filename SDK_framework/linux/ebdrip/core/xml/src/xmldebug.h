/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmldebug.h(EBDSDK_P.1) $
 * $Id: src:xmldebug.h,v 1.11.10.1.1.1 2013/12/19 11:25:09 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * XML debug interface.
 */

#ifndef __XMLDEBUG_H__
#define __XMLDEBUG_H__ (1)

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
extern int32 debug_xml ;
#endif

#if defined(DEBUG_BUILD)

#include "xml.h"

enum { /* Bitflags for XML debugging */
  DEBUG_XML_PARSE = 1,
  DEBUG_XML_STARTEND = 2,
  DEBUG_XML_ATTRIBUTES = 4,
  DEBUG_XML_CHARACTERS = 8
} ;

extern
Bool debug_xml_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter) ;

extern
Bool debug_xml_filter_dispose(
      xmlGFilter **filter) ;

#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLDEBUG_H__*/
