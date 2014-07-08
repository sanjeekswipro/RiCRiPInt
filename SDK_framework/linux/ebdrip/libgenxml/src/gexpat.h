#ifndef __GEXPAT_H__
#define __GEXPAT_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:gexpat.h(EBDSDK_P.1) $
 * $Id: src:gexpat.h,v 1.12.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 * 
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Expat back end private data structures.
 */

#include "xmlgpriv.h"
#include <expat.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xmlGParser {
  XML_Parser ctxt ;
  XML_Memory_Handling_Suite expat_mem ;

  xmlGParserCommon c ;
} ;

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__GEXPAT_H__*/
